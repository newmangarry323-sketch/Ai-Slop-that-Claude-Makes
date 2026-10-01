package io.github.newmangarry323sketch.rustbuilding.raid;

import java.lang.ref.WeakReference;
import java.util.Collection;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import net.minecraft.core.BlockPos;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.util.Mth;
import net.minecraft.world.level.Explosion;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.state.BlockState;

import io.github.newmangarry323sketch.rustbuilding.building.BuildingOps;
import io.github.newmangarry323sketch.rustbuilding.building.BuildingTier;
import io.github.newmangarry323sketch.rustbuilding.building.PieceLocator;
import io.github.newmangarry323sketch.rustbuilding.building.PieceRef;

/**
 * Raiding. Instead of an explosion deleting the blocks it reaches, it damages the piece they belong to,
 * and the whole piece falls once its damage reaches the grade's health - Rust's model.
 *
 * <p>A TNT block right against a piece deals {@value #TNT_DAMAGE}, so it takes 1 TNT for wood, 2 for
 * stone, 4 for sheet metal and 8 for armored. That mirrors how Rust raid costs scale between grades
 * (each grade has double the health of the one before it).
 */
public final class RaidDamage {
	public static final int TNT_DAMAGE = 250;
	private static final float TNT_POWER = 4.0F;

	// Several blocks of one piece are usually caught by the same blast; it should only count once.
	private static WeakReference<Explosion> currentExplosion = new WeakReference<>(null);
	private static final Set<Long> hitByCurrentExplosion = new HashSet<>();

	private RaidDamage() {
	}

	public static void onExplosionHit(ServerLevel level, BlockPos pos, BlockState state, Explosion explosion) {
		PieceRef ref = PieceLocator.locate(level, pos, state, explosion.center());

		if (ref == null) {
			// A stray block that no longer forms part of any piece just breaks.
			level.setBlock(pos, Blocks.AIR.defaultBlockState(), Block.UPDATE_ALL);
			return;
		}

		if (!firstHit(explosion, ref.anchor())) {
			return;
		}

		BuildingTier tier = ref.tier(level);

		if (tier == null) {
			return;
		}

		PieceDamage damage = PieceDamage.get(level);

		if (damage.add(ref.anchor(), damageFrom(explosion, ref.ownBlocks(level))) >= tier.health()) {
			BuildingOps.destroy(level, ref);
		}
	}

	/** Doors take damage the same way, keyed by their bottom half. */
	public static void onDoorHit(ServerLevel level, BlockPos lowerPos, int maxHealth, Explosion explosion) {
		if (!firstHit(explosion, lowerPos)) {
			return;
		}

		PieceDamage damage = PieceDamage.get(level);

		if (damage.add(lowerPos, damageFrom(explosion, List.of(lowerPos, lowerPos.above()))) >= maxHealth) {
			damage.clear(lowerPos);
			// Removing the bottom half takes the top half with it; neither drops anything.
			level.setBlock(lowerPos, Blocks.AIR.defaultBlockState(), Block.UPDATE_ALL);
		}
	}

	/**
	 * Damage scales with the blast's power and falls off with its distance from the nearest block of the
	 * piece: full strength up to one block away, a quarter at four blocks and beyond.
	 */
	public static int damageFrom(Explosion explosion, Collection<BlockPos> blocks) {
		double nearestSqr = Double.MAX_VALUE;

		for (BlockPos block : blocks) {
			nearestSqr = Math.min(nearestSqr, block.distToCenterSqr(explosion.center()));
		}

		if (nearestSqr == Double.MAX_VALUE) {
			return 0;
		}

		double falloff = Mth.clamp(1.25 - 0.25 * Math.sqrt(nearestSqr), 0.25, 1.0);
		return (int) Math.round(TNT_DAMAGE * (explosion.radius() / TNT_POWER) * falloff);
	}

	private static boolean firstHit(Explosion explosion, BlockPos anchor) {
		if (currentExplosion.get() != explosion) {
			currentExplosion = new WeakReference<>(explosion);
			hitByCurrentExplosion.clear();
		}

		return hitByCurrentExplosion.add(anchor.asLong());
	}

	/** Current health of a piece, for display. */
	public static int health(ServerLevel level, PieceRef ref) {
		BuildingTier tier = ref.tier(level);

		if (tier == null) {
			return 0;
		}

		return Math.max(0, tier.health() - PieceDamage.get(level).get(ref.anchor()));
	}

	public static boolean isDamaged(ServerLevel level, PieceRef ref) {
		return PieceDamage.get(level).get(ref.anchor()) > 0;
	}
}
