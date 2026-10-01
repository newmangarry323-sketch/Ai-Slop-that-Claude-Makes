package io.github.newmangarry323sketch.rustbuilding.upkeep;

import org.jspecify.annotations.Nullable;

import net.minecraft.core.BlockPos;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.phys.Vec3;

import io.github.newmangarry323sketch.rustbuilding.block.entity.ToolCupboardBlockEntity;
import io.github.newmangarry323sketch.rustbuilding.building.BuildingKind;
import io.github.newmangarry323sketch.rustbuilding.building.BuildingOps;
import io.github.newmangarry323sketch.rustbuilding.building.BuildingTier;
import io.github.newmangarry323sketch.rustbuilding.building.Grid;
import io.github.newmangarry323sketch.rustbuilding.building.PieceLocator;
import io.github.newmangarry323sketch.rustbuilding.building.PieceRef;
import io.github.newmangarry323sketch.rustbuilding.building.Structure;
import io.github.newmangarry323sketch.rustbuilding.privilege.BuildingPrivilege;
import io.github.newmangarry323sketch.rustbuilding.raid.PieceDamage;

/**
 * Rust's decay. A piece that no tool cupboard pays upkeep for loses health steadily and falls when it
 * reaches zero: from full health that takes Rust's 1, 3, 5, 8 and 12 hours for twig, wood, stone, sheet
 * metal and armored - here 1, 3, 5, 8 and 12 Minecraft days (see {@link Upkeep}). Doors and the
 * cupboard itself do not decay.
 *
 * <p>Pieces are checked when one of their own blocks gets a random tick, which happens only near
 * players. Each check counts all the time since the last one, so a base nobody visited still decays by
 * the right amount once someone does.
 */
public final class Decay {
	private Decay() {
	}

	/** How long a piece of this grade takes to decay from full health. */
	public static long duration(BuildingTier tier) {
		int rustHours = switch (tier) {
			case TWIG -> 1;
			case WOOD -> 3;
			case STONE -> 5;
			case METAL -> 8;
			case ARMORED -> 12;
		};
		return rustHours * Upkeep.RUST_HOUR;
	}

	/** Ticks per point of health lost; every duration divides evenly by its grade's health. */
	public static long ticksPerPoint(BuildingTier tier) {
		return duration(tier) / tier.health();
	}

	public enum Status {
		/** A cupboard pays for it. */
		PROTECTED,
		/** No cupboard pays for it. */
		DECAYING,
		/** Part of the area is not loaded, so a cupboard there could not be checked. */
		UNKNOWN
	}

	/**
	 * @param lapsedAt for a decaying piece covered by a cupboard that ran out, when its upkeep ran out;
	 *                 otherwise {@link DecayClocks#NONE}
	 */
	public record Protection(Status status, long lapsedAt) {
		static final Protection PROTECTED = new Protection(Status.PROTECTED, DecayClocks.NONE);
		static final Protection UNKNOWN = new Protection(Status.UNKNOWN, DecayClocks.NONE);
		static final Protection NO_CUPBOARD = new Protection(Status.DECAYING, DecayClocks.NONE);
	}

	/** Whether a cupboard covering this anchor pays for this grade now. Settles those cupboards first. */
	public static Protection protection(ServerLevel level, BlockPos anchor, BuildingTier tier, long now) {
		if (!BuildingPrivilege.areaLoaded(level, anchor, BuildingPrivilege.RANGE)) {
			return Protection.UNKNOWN;
		}

		boolean covered = false;
		long paidUntil = DecayClocks.NONE;

		for (ToolCupboardBlockEntity cupboard : BuildingPrivilege.cupboardsNear(level, anchor, BuildingPrivilege.RANGE)) {
			cupboard.settle(level, now);
			covered = true;
			paidUntil = Math.max(paidUntil, cupboard.paidUntil(tier));
		}

		if (!covered) {
			return Protection.NO_CUPBOARD;
		}

		return paidUntil >= now ? Protection.PROTECTED : new Protection(Status.DECAYING, paidUntil);
	}

	public static void onRandomTick(ServerLevel level, BlockPos pos) {
		PieceRef piece = pieceToCheck(level, pos, level.getBlockState(pos));

		if (piece != null) {
			apply(level, piece, level.getGameTime());
		}
	}

	/**
	 * The piece a block belongs to, if it is one of that piece's own blocks. Shared line blocks, corner
	 * pillars and support legs are left out, so a piece is checked about as often as it has blocks.
	 */
	@Nullable
	static PieceRef pieceToCheck(ServerLevel level, BlockPos pos, BlockState state) {
		if (Structure.isStairs(state)) {
			return PieceLocator.locate(level, pos, state, Vec3.atCenterOf(pos));
		}

		BuildingKind kind = Structure.kindOf(state);

		if (kind == BuildingKind.SLAB && Grid.isInterior(pos)) {
			return new PieceRef.Slab(Grid.cell(pos.getX()), Grid.cell(pos.getZ()), pos.getY());
		}

		if (kind == BuildingKind.WALL && Grid.onLine(pos.getX()) != Grid.onLine(pos.getZ())) {
			return PieceLocator.locate(level, pos, state, Vec3.atCenterOf(pos));
		}

		return null;
	}

	/**
	 * Brings a piece's decay up to {@code now}: nothing while a cupboard pays for it; otherwise it loses
	 * health for the time since it was last counted, or since its upkeep ran out, and falls at zero.
	 *
	 * @return whether the piece fell
	 */
	public static boolean apply(ServerLevel level, PieceRef piece, long now) {
		BuildingTier tier = piece.tier(level);

		if (tier == null) {
			return false;
		}

		Protection protection = protection(level, piece.anchor(), tier, now);

		if (protection.status() == Status.UNKNOWN) {
			return false;
		}

		DecayClocks clocks = DecayClocks.get(level);
		BlockPos anchor = piece.anchor();

		if (protection.status() == Status.PROTECTED) {
			clocks.clear(anchor);
			return false;
		}

		// Count from the last check, but never from before the upkeep ran out; a piece seen for the first
		// time without any cupboard starts counting now.
		long since = clocks.get(anchor);
		long start;

		if (since != DecayClocks.NONE) {
			start = Math.max(since, protection.lapsedAt());
		} else {
			start = protection.lapsedAt() != DecayClocks.NONE ? protection.lapsedAt() : now;
		}

		start = Math.min(start, now);
		long perPoint = ticksPerPoint(tier);
		long points = (now - start) / perPoint;

		if (points <= 0) {
			clocks.set(anchor, start);
			return false;
		}

		clocks.set(anchor, start + points * perPoint);
		int damage = PieceDamage.get(level).add(anchor, (int) Math.min(points, tier.health()));

		if (damage >= tier.health()) {
			BuildingOps.destroy(level, piece);
			return true;
		}

		return false;
	}
}
