package io.github.newmangarry323sketch.rustbuilding;

import java.util.HashMap;
import java.util.Map;
import java.util.UUID;

import net.minecraft.ChatFormatting;
import net.minecraft.core.BlockPos;
import net.minecraft.network.chat.Component;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.InteractionResult;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.item.BlockItem;
import net.minecraft.world.item.context.BlockPlaceContext;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.block.TntBlock;
import net.minecraft.world.level.block.state.BlockState;

import net.fabricmc.fabric.api.event.player.AttackBlockCallback;
import net.fabricmc.fabric.api.event.player.ItemEvents;
import net.fabricmc.fabric.api.event.player.PlayerBlockBreakEvents;

import io.github.newmangarry323sketch.rustbuilding.block.RustDoorBlock;
import io.github.newmangarry323sketch.rustbuilding.block.ToolCupboardBlock;
import io.github.newmangarry323sketch.rustbuilding.building.HammerActions;
import io.github.newmangarry323sketch.rustbuilding.building.Structure;
import io.github.newmangarry323sketch.rustbuilding.lock.LockHolder;
import io.github.newmangarry323sketch.rustbuilding.privilege.BuildingPrivilege;
import io.github.newmangarry323sketch.rustbuilding.registry.ModItems;

/**
 * Building privilege for everything that is not a building piece: placing any block (except TNT)
 * inside someone else's cupboard zone is blocked, and their cupboards and doors cannot be picked up.
 * Other blocks can still be broken, as deployables can be destroyed in Rust.
 */
public final class ProtectionEvents {
	private static final Map<UUID, Long> LAST_HAMMER_HIT = new HashMap<>();

	private ProtectionEvents() {
	}

	public static void init() {
		PlayerBlockBreakEvents.BEFORE.register((level, player, pos, state, blockEntity) -> canPickUp(level, player, pos, state));

		ItemEvents.USE_ON.register(context -> {
			Player player = context.getPlayer();

			if (player == null || !(context.getItemInHand().getItem() instanceof BlockItem blockItem)) {
				return null;
			}

			// Explosives are not building: raiders may set TNT against a base, as they throw C4 in Rust.
			if (blockItem.getBlock() instanceof TntBlock) {
				return null;
			}

			Level level = context.getLevel();
			BlockPos target = new BlockPlaceContext(context).getClickedPos();

			if (!BuildingPrivilege.canBuild(level, player, target)) {
				deny(level, player, "building_blocked");
				return InteractionResult.FAIL;
			}

			if (blockItem.getBlock() instanceof ToolCupboardBlock && !BuildingPrivilege.canPlaceCupboard(level, player, target)) {
				deny(level, player, "cupboard_overlap");
				return InteractionResult.FAIL;
			}

			return null;
		});

		// The hammer's left click repairs and inspects pieces instead of mining them.
		AttackBlockCallback.EVENT.register((player, level, hand, pos, direction) -> {
			if (!player.getItemInHand(hand).is(ModItems.HAMMER) || !Structure.isPiece(level.getBlockState(pos))) {
				return InteractionResult.PASS;
			}

			if (level instanceof ServerLevel serverLevel) {
				long now = serverLevel.getGameTime();
				Long last = LAST_HAMMER_HIT.get(player.getUUID());

				if (last == null || now - last >= 10) {
					LAST_HAMMER_HIT.put(player.getUUID(), now);
					HammerActions.repairOrInspect(serverLevel, player, pos);
				}
			}

			return InteractionResult.SUCCESS;
		});
	}

	private static boolean canPickUp(Level level, Player player, BlockPos pos, BlockState state) {
		if (BuildingPrivilege.bypasses(player)) {
			return true;
		}

		if (!(state.getBlock() instanceof ToolCupboardBlock) && !(state.getBlock() instanceof RustDoorBlock)) {
			return true;
		}

		if (!BuildingPrivilege.canBuild(level, player, pos)) {
			deny(level, player, "building_blocked");
			return false;
		}

		LockHolder holder = LockHolder.find(level, pos);

		if (holder != null && !holder.lock().canAccess(player)) {
			deny(level, player, "locked");
			return false;
		}

		return true;
	}

	private static void deny(Level level, Player player, String reason) {
		if (!level.isClientSide()) {
			player.sendOverlayMessage(Component.translatable("message.rustbuilding." + reason).withStyle(ChatFormatting.RED));
		}
	}
}
