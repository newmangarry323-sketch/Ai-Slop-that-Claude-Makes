package io.github.newmangarry323sketch.rustbuilding.building;

import java.util.List;

import org.jspecify.annotations.Nullable;

import net.minecraft.ChatFormatting;
import net.minecraft.core.BlockPos;
import net.minecraft.network.chat.Component;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.sounds.SoundSource;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.level.block.state.BlockState;

import io.github.newmangarry323sketch.rustbuilding.privilege.BuildingPrivilege;
import io.github.newmangarry323sketch.rustbuilding.raid.PieceDamage;
import io.github.newmangarry323sketch.rustbuilding.raid.RaidDamage;
import io.github.newmangarry323sketch.rustbuilding.registry.ModBlocks;

/** What the hammer does on the server. Every action re-checks reach and building privilege. */
public final class HammerActions {
	private static final double MAX_DISTANCE_SQR = 8.0 * 8.0;

	private HammerActions() {
	}

	@Nullable
	private static PieceRef locate(ServerLevel level, Player player, BlockPos pos) {
		if (pos.distToCenterSqr(player.getEyePosition()) > MAX_DISTANCE_SQR) {
			return null;
		}

		BlockState state = level.getBlockState(pos);
		return PieceLocator.locate(level, pos, state, player.getEyePosition());
	}

	/** Upgrades to {@code requested}, or to the next grade when it is null. */
	public static void upgrade(ServerLevel level, Player player, BlockPos pos, @Nullable BuildingTier requested) {
		PieceRef ref = locate(level, player, pos);
		BuildingTier current = ref == null ? null : ref.tier(level);

		if (ref == null || current == null) {
			return;
		}

		BuildingTier target = requested != null ? requested : current.next();

		if (target == null) {
			fail(player, Component.translatable("message.rustbuilding.max_grade"));
			return;
		}

		if (target.ordinal() <= current.ordinal()) {
			fail(player, Component.translatable("message.rustbuilding.already_grade", target.displayName()));
			return;
		}

		List<BlockPos> own = ref.ownBlocks(level);

		if (!BuildingPrivilege.canBuildAll(level, player, own)) {
			fail(player, Component.translatable("message.rustbuilding.building_blocked"));
			return;
		}

		int cost = target.cost(ref.units(level));

		if (!Costs.take(player, target, cost)) {
			fail(player, Component.translatable("message.rustbuilding.need", Costs.describe(target, cost)));
			return;
		}

		BuildingOps.upgrade(level, ref, target);
		BlockState sample = ModBlocks.building(target).defaultBlockState();
		level.playSound(null, pos, sample.getSoundType().getPlaceSound(), SoundSource.BLOCKS, 1.0F, 0.8F);
		player.sendOverlayMessage(Component.translatable("message.rustbuilding.upgraded", ref.describe(level)).withStyle(ChatFormatting.GREEN));
	}

	/** Takes a piece down and refunds half of what its current grade cost. */
	public static void demolish(ServerLevel level, Player player, BlockPos pos) {
		PieceRef ref = locate(level, player, pos);
		BuildingTier tier = ref == null ? null : ref.tier(level);

		if (ref == null || tier == null) {
			return;
		}

		if (!BuildingPrivilege.canBuildAll(level, player, ref.ownBlocks(level))) {
			fail(player, Component.translatable("message.rustbuilding.building_blocked"));
			return;
		}

		Component name = ref.describe(level);
		int refund = tier.cost(ref.units(level)) / 2;
		BlockState sample = level.getBlockState(pos);
		BuildingOps.destroy(level, ref);
		Costs.give(player, tier, refund);
		level.playSound(null, pos, sample.getSoundType().getBreakSound(), SoundSource.BLOCKS, 1.0F, 0.9F);
		player.sendOverlayMessage(Component.translatable("message.rustbuilding.demolished", name));
	}

	/** Left click: repairs a damaged piece, or reports its health. */
	public static void repairOrInspect(ServerLevel level, Player player, BlockPos pos) {
		PieceRef ref = locate(level, player, pos);
		BuildingTier tier = ref == null ? null : ref.tier(level);

		if (ref == null || tier == null) {
			return;
		}

		int damage = PieceDamage.get(level).get(ref.anchor());

		if (damage <= 0) {
			player.sendOverlayMessage(Component.translatable("message.rustbuilding.health",
					ref.describe(level), RaidDamage.health(level, ref), tier.health()));
			return;
		}

		if (!BuildingPrivilege.canBuildAll(level, player, ref.ownBlocks(level))) {
			fail(player, Component.translatable("message.rustbuilding.building_blocked"));
			return;
		}

		// A full repair costs half of what the grade cost to build; a partial one, its share of that.
		int full = tier.cost(ref.units(level));
		int cost = Math.max(1, (int) Math.ceil(full * Math.min(1.0, damage / (double) tier.health()) / 2.0));

		if (!Costs.take(player, tier, cost)) {
			fail(player, Component.translatable("message.rustbuilding.need", Costs.describe(tier, cost)));
			return;
		}

		PieceDamage.get(level).clear(ref.anchor());
		level.playSound(null, pos, level.getBlockState(pos).getSoundType().getHitSound(), SoundSource.BLOCKS, 1.0F, 1.2F);
		player.sendOverlayMessage(Component.translatable("message.rustbuilding.repaired", ref.describe(level), tier.health(), tier.health())
				.withStyle(ChatFormatting.GREEN));
	}

	private static void fail(Player player, Component message) {
		player.sendOverlayMessage(message.copy().withStyle(ChatFormatting.RED));
	}
}
