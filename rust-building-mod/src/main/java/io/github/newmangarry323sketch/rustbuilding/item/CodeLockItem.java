package io.github.newmangarry323sketch.rustbuilding.item;

import java.util.function.Consumer;

import net.minecraft.ChatFormatting;
import net.minecraft.core.BlockPos;
import net.minecraft.network.chat.Component;
import net.minecraft.world.InteractionResult;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.item.Item;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.TooltipFlag;
import net.minecraft.world.item.component.TooltipDisplay;
import net.minecraft.world.item.context.UseOnContext;
import net.minecraft.world.level.Level;

import io.github.newmangarry323sketch.rustbuilding.ClientBridge;
import io.github.newmangarry323sketch.rustbuilding.LockScreenMode;
import io.github.newmangarry323sketch.rustbuilding.lock.LockHolder;
import io.github.newmangarry323sketch.rustbuilding.privilege.BuildingPrivilege;

/**
 * A code lock. Use it on a Rust door or a tool cupboard and choose a four-digit code; the lock is
 * fitted (and the item used up) when the code is confirmed.
 */
public class CodeLockItem extends Item {
	public CodeLockItem(Properties properties) {
		super(properties);
	}

	@Override
	public InteractionResult useOn(UseOnContext context) {
		Level level = context.getLevel();
		Player player = context.getPlayer();
		LockHolder holder = LockHolder.find(level, context.getClickedPos());

		if (player == null || holder == null) {
			return InteractionResult.PASS;
		}

		BlockPos lockPos = holder.getBlockPos();

		if (holder.lock().isLocked()) {
			if (!level.isClientSide()) {
				player.sendOverlayMessage(Component.translatable("message.rustbuilding.already_locked").withStyle(ChatFormatting.RED));
			}

			return InteractionResult.FAIL;
		}

		if (!BuildingPrivilege.canBuild(level, player, lockPos)) {
			if (!level.isClientSide()) {
				player.sendOverlayMessage(Component.translatable("message.rustbuilding.building_blocked").withStyle(ChatFormatting.RED));
			}

			return InteractionResult.FAIL;
		}

		if (level.isClientSide()) {
			ClientBridge.get().openCodeLock(lockPos, LockScreenMode.SET);
		}

		return InteractionResult.SUCCESS;
	}

	@Override
	public void appendHoverText(ItemStack stack, TooltipContext context, TooltipDisplay display, Consumer<Component> tooltip, TooltipFlag flag) {
		tooltip.accept(Component.translatable("item.rustbuilding.code_lock.hint").withStyle(ChatFormatting.GRAY));
	}
}
