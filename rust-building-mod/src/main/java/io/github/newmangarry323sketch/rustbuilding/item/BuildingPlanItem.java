package io.github.newmangarry323sketch.rustbuilding.item;

import java.util.function.Consumer;

import net.minecraft.ChatFormatting;
import net.minecraft.network.chat.Component;
import net.minecraft.sounds.SoundSource;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.InteractionResult;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.item.Item;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.TooltipFlag;
import net.minecraft.world.item.component.TooltipDisplay;
import net.minecraft.world.level.Level;

import io.github.newmangarry323sketch.rustbuilding.ClientBridge;
import io.github.newmangarry323sketch.rustbuilding.building.BuildingOps;
import io.github.newmangarry323sketch.rustbuilding.building.BuildingTier;
import io.github.newmangarry323sketch.rustbuilding.building.Costs;
import io.github.newmangarry323sketch.rustbuilding.building.PiecePlanner;
import io.github.newmangarry323sketch.rustbuilding.building.PieceType;
import io.github.newmangarry323sketch.rustbuilding.building.PlannedPiece;
import io.github.newmangarry323sketch.rustbuilding.registry.ModBlocks;
import io.github.newmangarry323sketch.rustbuilding.registry.ModComponents;

/**
 * Rust's building plan. Use it to place the selected piece, in twig, where the preview shows;
 * sneak-use it to pick a different piece.
 */
public class BuildingPlanItem extends Item {
	public BuildingPlanItem(Properties properties) {
		super(properties);
	}

	public static PieceType selected(ItemStack stack) {
		return PieceType.byOrdinal(stack.getOrDefault(ModComponents.SELECTED_PIECE, 0));
	}

	@Override
	public InteractionResult use(Level level, Player player, InteractionHand hand) {
		if (player.isShiftKeyDown()) {
			if (level.isClientSide()) {
				ClientBridge.get().openPieceMenu();
			}

			return InteractionResult.SUCCESS;
		}

		if (level.isClientSide()) {
			return InteractionResult.SUCCESS;
		}

		PieceType type = selected(player.getItemInHand(hand));
		PlannedPiece plan = PiecePlanner.plan(level, player, type);

		if (plan == null) {
			player.sendOverlayMessage(Component.translatable("message.rustbuilding.aim." + type.category().name().toLowerCase(java.util.Locale.ROOT))
					.withStyle(ChatFormatting.YELLOW));
			return InteractionResult.FAIL;
		}

		if (!plan.valid()) {
			player.sendOverlayMessage(plan.problem().copy().withStyle(ChatFormatting.RED));
			return InteractionResult.FAIL;
		}

		int cost = BuildingTier.TWIG.cost(plan.units());

		if (!Costs.take(player, BuildingTier.TWIG, cost)) {
			player.sendOverlayMessage(Component.translatable("message.rustbuilding.need", Costs.describe(BuildingTier.TWIG, cost))
					.withStyle(ChatFormatting.RED));
			return InteractionResult.FAIL;
		}

		BuildingOps.place(level, plan);
		level.playSound(null, plan.ref().anchor(),
				ModBlocks.building(BuildingTier.TWIG).defaultBlockState().getSoundType().getPlaceSound(),
				SoundSource.BLOCKS, 1.0F, 0.9F);
		return InteractionResult.SUCCESS;
	}

	@Override
	public void appendHoverText(ItemStack stack, TooltipContext context, TooltipDisplay display, Consumer<Component> tooltip, TooltipFlag flag) {
		tooltip.accept(Component.translatable("item.rustbuilding.building_plan.selected", selected(stack).displayName()).withStyle(ChatFormatting.GRAY));
		tooltip.accept(Component.translatable("item.rustbuilding.building_plan.hint").withStyle(ChatFormatting.DARK_GRAY));
	}
}
