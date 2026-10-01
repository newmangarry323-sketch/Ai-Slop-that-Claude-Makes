package io.github.newmangarry323sketch.rustbuilding.client;

import java.util.ArrayList;
import java.util.List;

import net.minecraft.ChatFormatting;
import net.minecraft.client.DeltaTracker;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.network.chat.Component;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.HitResult;

import net.fabricmc.fabric.api.client.rendering.v1.hud.HudElementRegistry;
import net.fabricmc.fabric.api.client.rendering.v1.hud.VanillaHudElements;

import io.github.newmangarry323sketch.rustbuilding.RustBuilding;
import io.github.newmangarry323sketch.rustbuilding.building.BuildingTier;
import io.github.newmangarry323sketch.rustbuilding.building.Costs;
import io.github.newmangarry323sketch.rustbuilding.building.PieceLocator;
import io.github.newmangarry323sketch.rustbuilding.building.PieceRef;
import io.github.newmangarry323sketch.rustbuilding.building.PlannedPiece;
import io.github.newmangarry323sketch.rustbuilding.building.Structure;
import io.github.newmangarry323sketch.rustbuilding.item.BuildingPlanItem;
import io.github.newmangarry323sketch.rustbuilding.registry.ModItems;

/** A few lines under the crosshair saying what the plan or hammer in hand is about to do. */
public final class BuildingHud {
	private static final int WHITE = 0xFFFFFFFF;
	private static final int GREY = 0xFFAAAAAA;
	private static final int RED = 0xFFFF5555;
	private static final int GOLD = 0xFFFFAA00;

	private record Line(String text, int color) {
	}

	private BuildingHud() {
	}

	public static void init() {
		HudElementRegistry.attachElementAfter(VanillaHudElements.CROSSHAIR, RustBuilding.id("building_hud"), BuildingHud::extract);
	}

	private static void extract(GuiGraphicsExtractor graphics, DeltaTracker deltaTracker) {
		Minecraft minecraft = Minecraft.getInstance();

		if (minecraft.player == null || minecraft.level == null) {
			return;
		}

		List<Line> lines = lines(minecraft, minecraft.player);
		int width = graphics.guiWidth();
		int y = graphics.guiHeight() / 2 + 14;

		for (Line line : lines) {
			graphics.text(minecraft.font, line.text(), (width - minecraft.font.width(line.text())) / 2, y, line.color(), true);
			y += 11;
		}
	}

	private static List<Line> lines(Minecraft minecraft, LocalPlayer player) {
		List<Line> lines = new ArrayList<>();
		ItemStack planStack = PlacementPreview.heldPlan(player);

		if (planStack != null) {
			Component piece = Component.translatable("piece.rustbuilding.described", BuildingTier.TWIG.displayName(),
					BuildingPlanItem.selected(planStack).displayName());
			lines.add(new Line(piece.getString(), WHITE));
			PlannedPiece plan = PlacementPreview.currentPlan();

			if (plan == null) {
				String aim = "message.rustbuilding.aim." + BuildingPlanItem.selected(planStack).category().name().toLowerCase(java.util.Locale.ROOT);
				lines.add(new Line(Component.translatable(aim).getString(), GREY));
			} else {
				int cost = BuildingTier.TWIG.cost(plan.units());
				boolean affordable = Costs.canAfford(player, BuildingTier.TWIG, cost);
				lines.add(new Line(Costs.describe(BuildingTier.TWIG, cost).getString(), affordable ? GREY : RED));

				if (!plan.valid() && plan.problem() != null) {
					lines.add(new Line(plan.problem().getString(), RED));
				}
			}

			lines.add(new Line(Component.translatable("hud.rustbuilding.plan_keys").getString(), GREY));
			return lines;
		}

		boolean hammer = player.getMainHandItem().is(ModItems.HAMMER) || player.getOffhandItem().is(ModItems.HAMMER);

		if (!hammer || player.isSpectator()) {
			return lines;
		}

		HitResult hit = minecraft.hitResult;

		if (!(hit instanceof BlockHitResult blockHit) || hit.getType() == HitResult.Type.MISS) {
			return lines;
		}

		BlockState state = minecraft.level.getBlockState(blockHit.getBlockPos());

		if (!Structure.isPiece(state)) {
			return lines;
		}

		PieceRef ref = PieceLocator.locate(minecraft.level, blockHit.getBlockPos(), state, player.getEyePosition());

		if (ref == null) {
			return lines;
		}

		lines.add(new Line(ref.describe(minecraft.level).getString(), WHITE));
		BuildingTier tier = ref.tier(minecraft.level);
		BuildingTier next = tier == null ? null : tier.next();

		if (next != null) {
			int cost = next.cost(ref.units(minecraft.level));
			boolean affordable = Costs.canAfford(player, next, cost);
			Component upgrade = Component.translatable("hud.rustbuilding.upgrade", next.displayName(), Costs.describe(next, cost));
			lines.add(new Line(upgrade.getString(), affordable ? GOLD : RED));
		} else {
			lines.add(new Line(Component.translatable("message.rustbuilding.max_grade").withStyle(ChatFormatting.GRAY).getString(), GREY));
		}

		lines.add(new Line(Component.translatable("hud.rustbuilding.hammer_keys").getString(), GREY));
		return lines;
	}
}
