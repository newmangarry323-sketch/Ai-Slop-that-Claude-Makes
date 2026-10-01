package io.github.newmangarry323sketch.rustbuilding.item;

import java.util.function.Consumer;

import net.minecraft.ChatFormatting;
import net.minecraft.core.BlockPos;
import net.minecraft.network.chat.Component;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.InteractionResult;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.item.Item;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.TooltipFlag;
import net.minecraft.world.item.component.TooltipDisplay;
import net.minecraft.world.item.context.UseOnContext;
import net.minecraft.world.level.Level;

import io.github.newmangarry323sketch.rustbuilding.ClientBridge;
import io.github.newmangarry323sketch.rustbuilding.building.HammerActions;
import io.github.newmangarry323sketch.rustbuilding.building.Structure;

/**
 * Rust's hammer. Use on a piece to upgrade it one grade; sneak-use for the menu (any grade, or
 * demolish); hit a piece to repair it or see its health.
 */
public class HammerItem extends Item {
	public HammerItem(Properties properties) {
		super(properties);
	}

	@Override
	public InteractionResult useOn(UseOnContext context) {
		Level level = context.getLevel();
		Player player = context.getPlayer();
		BlockPos pos = context.getClickedPos();

		if (player == null || !Structure.isPiece(level.getBlockState(pos))) {
			return InteractionResult.PASS;
		}

		if (player.isShiftKeyDown()) {
			if (level.isClientSide()) {
				ClientBridge.get().openHammerMenu(pos);
			}

			return InteractionResult.SUCCESS;
		}

		if (level instanceof ServerLevel serverLevel) {
			HammerActions.upgrade(serverLevel, player, pos, null);
		}

		return InteractionResult.SUCCESS;
	}

	@Override
	public void appendHoverText(ItemStack stack, TooltipContext context, TooltipDisplay display, Consumer<Component> tooltip, TooltipFlag flag) {
		tooltip.accept(Component.translatable("item.rustbuilding.hammer.hint").withStyle(ChatFormatting.GRAY));
	}
}
