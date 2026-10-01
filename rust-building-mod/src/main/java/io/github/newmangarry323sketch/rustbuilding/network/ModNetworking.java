package io.github.newmangarry323sketch.rustbuilding.network;

import net.minecraft.ChatFormatting;
import net.minecraft.core.BlockPos;
import net.minecraft.network.chat.Component;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.sounds.SoundEvents;
import net.minecraft.sounds.SoundSource;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.item.ItemStack;

import net.fabricmc.fabric.api.networking.v1.PayloadTypeRegistry;
import net.fabricmc.fabric.api.networking.v1.ServerPlayNetworking;

import io.github.newmangarry323sketch.rustbuilding.block.entity.ToolCupboardBlockEntity;
import io.github.newmangarry323sketch.rustbuilding.building.BuildingTier;
import io.github.newmangarry323sketch.rustbuilding.building.Costs;
import io.github.newmangarry323sketch.rustbuilding.building.HammerActions;
import io.github.newmangarry323sketch.rustbuilding.building.PieceType;
import io.github.newmangarry323sketch.rustbuilding.lock.CodeLock;
import io.github.newmangarry323sketch.rustbuilding.lock.LockHolder;
import io.github.newmangarry323sketch.rustbuilding.privilege.BuildingPrivilege;
import io.github.newmangarry323sketch.rustbuilding.registry.ModComponents;
import io.github.newmangarry323sketch.rustbuilding.registry.ModItems;

/** Registers the payloads and handles them on the server (Fabric runs these handlers on the server thread). */
public final class ModNetworking {
	private static final double REACH_SQR = 8.0 * 8.0;
	/** Rust's code lock gives an electric shock for a wrong code; this is ours. */
	private static final float WRONG_CODE_DAMAGE = 2.0F;

	private ModNetworking() {
	}

	public static void init() {
		PayloadTypeRegistry.serverboundPlay().register(Payloads.SelectPiece.TYPE, Payloads.SelectPiece.CODEC);
		PayloadTypeRegistry.serverboundPlay().register(Payloads.HammerAction.TYPE, Payloads.HammerAction.CODEC);
		PayloadTypeRegistry.serverboundPlay().register(Payloads.CupboardAction.TYPE, Payloads.CupboardAction.CODEC);
		PayloadTypeRegistry.serverboundPlay().register(Payloads.CodeLockAction.TYPE, Payloads.CodeLockAction.CODEC);

		ServerPlayNetworking.registerGlobalReceiver(Payloads.SelectPiece.TYPE, (payload, context) -> selectPiece(context.player(), payload));
		ServerPlayNetworking.registerGlobalReceiver(Payloads.HammerAction.TYPE, (payload, context) -> hammer(context.player(), payload));
		ServerPlayNetworking.registerGlobalReceiver(Payloads.CupboardAction.TYPE, (payload, context) -> cupboard(context.player(), payload));
		ServerPlayNetworking.registerGlobalReceiver(Payloads.CodeLockAction.TYPE, (payload, context) -> codeLock(context.player(), payload));
	}

	private static void selectPiece(ServerPlayer player, Payloads.SelectPiece payload) {
		if (payload.piece() < 0 || payload.piece() >= PieceType.values().length) {
			return;
		}

		for (InteractionHand hand : InteractionHand.values()) {
			ItemStack stack = player.getItemInHand(hand);

			if (stack.is(ModItems.BUILDING_PLAN)) {
				stack.set(ModComponents.SELECTED_PIECE, payload.piece());
				return;
			}
		}
	}

	private static void hammer(ServerPlayer player, Payloads.HammerAction payload) {
		if (!holding(player, ModItems.HAMMER) || !inReach(player, payload.pos())) {
			return;
		}

		ServerLevel level = (ServerLevel) player.level();

		switch (payload.action()) {
			case Payloads.HammerAction.UPGRADE -> {
				if (payload.tier() >= 0 && payload.tier() < BuildingTier.values().length) {
					HammerActions.upgrade(level, player, payload.pos(), BuildingTier.values()[payload.tier()]);
				}
			}
			case Payloads.HammerAction.DEMOLISH -> HammerActions.demolish(level, player, payload.pos());
			case Payloads.HammerAction.REPAIR -> HammerActions.repairOrInspect(level, player, payload.pos());
			default -> {
			}
		}
	}

	private static void cupboard(ServerPlayer player, Payloads.CupboardAction payload) {
		ServerLevel level = (ServerLevel) player.level();

		if (!inReach(player, payload.pos()) || !(level.getBlockEntity(payload.pos()) instanceof ToolCupboardBlockEntity cupboard)) {
			return;
		}

		if (!cupboard.lock().canAccess(player)) {
			player.sendOverlayMessage(Component.translatable("message.rustbuilding.locked").withStyle(ChatFormatting.RED));
			return;
		}

		switch (payload.action()) {
			case Payloads.CupboardAction.AUTHORIZE -> cupboard.authorize(player);
			case Payloads.CupboardAction.DEAUTHORIZE -> cupboard.deauthorize(player);
			case Payloads.CupboardAction.CLEAR -> cupboard.clearAuthorized();
			default -> {
			}
		}
	}

	private static void codeLock(ServerPlayer player, Payloads.CodeLockAction payload) {
		ServerLevel level = (ServerLevel) player.level();
		LockHolder holder = LockHolder.find(level, payload.pos());

		if (holder == null || !inReach(player, payload.pos())) {
			return;
		}

		CodeLock lock = holder.lock();
		String code = payload.code();

		switch (payload.action()) {
			case Payloads.CodeLockAction.INSTALL -> {
				if (lock.isLocked() || !CodeLock.isValidCode(code) || !BuildingPrivilege.canBuild(level, player, holder.getBlockPos())) {
					return;
				}

				if (!player.hasInfiniteMaterials()) {
					ItemStack stack = findHeld(player);

					if (stack == null) {
						return;
					}

					stack.shrink(1);
				}

				lock.install(player, code);
				holder.lockChanged();
				click(level, holder.getBlockPos(), 1.2F);
				player.sendOverlayMessage(Component.translatable("message.rustbuilding.lock_installed").withStyle(ChatFormatting.GREEN));
			}
			case Payloads.CodeLockAction.ENTER -> {
				if (!lock.isLocked()) {
					return;
				}

				if (lock.tryCode(player, code)) {
					holder.lockChanged();
					click(level, holder.getBlockPos(), 1.4F);
					player.sendOverlayMessage(Component.translatable("message.rustbuilding.code_accepted").withStyle(ChatFormatting.GREEN));
				} else {
					player.hurtServer(level, level.damageSources().magic(), WRONG_CODE_DAMAGE);
					click(level, holder.getBlockPos(), 0.6F);
					player.sendOverlayMessage(Component.translatable("message.rustbuilding.code_wrong").withStyle(ChatFormatting.RED));
				}
			}
			case Payloads.CodeLockAction.CHANGE -> {
				if (lock.isLocked() && lock.isOwner(player) && CodeLock.isValidCode(code)) {
					lock.changeCode(code);
					holder.lockChanged();
					click(level, holder.getBlockPos(), 1.2F);
					player.sendOverlayMessage(Component.translatable("message.rustbuilding.code_changed").withStyle(ChatFormatting.GREEN));
				}
			}
			case Payloads.CodeLockAction.REMOVE -> {
				if (lock.isLocked() && lock.isOwner(player)) {
					lock.remove();
					holder.lockChanged();
					Costs.giveOrDrop(player, new ItemStack(ModItems.CODE_LOCK));
					player.sendOverlayMessage(Component.translatable("message.rustbuilding.lock_removed"));
				}
			}
			default -> {
			}
		}
	}

	private static ItemStack findHeld(ServerPlayer player) {
		for (InteractionHand hand : InteractionHand.values()) {
			ItemStack stack = player.getItemInHand(hand);

			if (stack.is(ModItems.CODE_LOCK)) {
				return stack;
			}
		}

		return null;
	}

	private static boolean holding(ServerPlayer player, net.minecraft.world.item.Item item) {
		return player.getMainHandItem().is(item) || player.getOffhandItem().is(item);
	}

	private static boolean inReach(ServerPlayer player, BlockPos pos) {
		return pos.distToCenterSqr(player.getEyePosition()) <= REACH_SQR;
	}

	private static void click(ServerLevel level, BlockPos pos, float pitch) {
		level.playSound(null, pos, SoundEvents.IRON_TRAPDOOR_CLOSE, SoundSource.BLOCKS, 0.6F, pitch);
	}
}
