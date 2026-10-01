package io.github.newmangarry323sketch.rustbuilding.client;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import org.jspecify.annotations.Nullable;

import com.mojang.blaze3d.vertex.PoseStack;

import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.client.renderer.rendertype.RenderTypes;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.phys.Vec3;

import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.fabricmc.fabric.api.client.rendering.v1.level.LevelRenderContext;
import net.fabricmc.fabric.api.client.rendering.v1.level.LevelRenderEvents;

import io.github.newmangarry323sketch.rustbuilding.building.PiecePlanner;
import io.github.newmangarry323sketch.rustbuilding.building.PieceType;
import io.github.newmangarry323sketch.rustbuilding.building.PlannedPiece;
import io.github.newmangarry323sketch.rustbuilding.item.BuildingPlanItem;
import io.github.newmangarry323sketch.rustbuilding.registry.ModItems;

/**
 * The see-through blueprint of the piece the building plan would place: blue where it fits, red where
 * it does not, like Rust's placement preview. Planned once per tick, drawn every frame.
 */
public final class PlacementPreview {
	private static final int VALID_COLOR = 0x553D9BFF;
	private static final int INVALID_COLOR = 0x55FF3B3B;
	private static final float INFLATE = 0.004F;

	@Nullable
	private static PlannedPiece plan;
	@Nullable
	private static Preview preview;

	private record Preview(float[] vertices, int color) {
	}

	private PlacementPreview() {
	}

	public static void init() {
		ClientTickEvents.END_CLIENT_TICK.register(PlacementPreview::tick);
		LevelRenderEvents.BEFORE_TRANSLUCENT_TERRAIN.register(PlacementPreview::render);
	}

	/** The piece being previewed this tick, if the player holds a building plan and aims somewhere useful. */
	@Nullable
	public static PlannedPiece currentPlan() {
		return plan;
	}

	@Nullable
	public static ItemStack heldPlan(LocalPlayer player) {
		if (player.getMainHandItem().is(ModItems.BUILDING_PLAN)) {
			return player.getMainHandItem();
		}

		return player.getOffhandItem().is(ModItems.BUILDING_PLAN) ? player.getOffhandItem() : null;
	}

	private static void tick(Minecraft minecraft) {
		plan = null;
		preview = null;

		if (minecraft.player == null || minecraft.level == null) {
			return;
		}

		ItemStack stack = heldPlan(minecraft.player);

		if (stack == null) {
			return;
		}

		PieceType type = BuildingPlanItem.selected(stack);
		plan = PiecePlanner.plan(minecraft.level, minecraft.player, type);

		if (plan != null) {
			preview = new Preview(outline(plan.positions()), plan.valid() ? VALID_COLOR : INVALID_COLOR);
		}
	}

	private static void render(LevelRenderContext context) {
		Preview current = preview;

		if (current == null) {
			return;
		}

		Vec3 camera = context.levelState().cameraRenderState.pos;
		PoseStack poseStack = context.poseStack();
		poseStack.pushPose();
		poseStack.translate(-camera.x, -camera.y, -camera.z);
		context.submitNodeCollector().submitCustomGeometry(poseStack, RenderTypes.debugFilledBox(), (pose, buffer) -> {
			float[] v = current.vertices();

			for (int i = 0; i < v.length; i += 3) {
				buffer.addVertex(pose, v[i], v[i + 1], v[i + 2]).setColor(current.color());
			}
		});
		poseStack.popPose();
	}

	/** Quads for the outer faces only, so the blueprint reads as one solid shape. */
	private static float[] outline(List<BlockPos> positions) {
		Set<BlockPos> set = new HashSet<>(positions);
		List<Float> vertices = new ArrayList<>();

		for (BlockPos pos : positions) {
			float x0 = pos.getX() - INFLATE;
			float y0 = pos.getY() - INFLATE;
			float z0 = pos.getZ() - INFLATE;
			float x1 = pos.getX() + 1 + INFLATE;
			float y1 = pos.getY() + 1 + INFLATE;
			float z1 = pos.getZ() + 1 + INFLATE;

			for (Direction direction : Direction.values()) {
				if (set.contains(pos.relative(direction))) {
					continue;
				}

				// Each face wound counter-clockwise as seen from outside the block.
				float[] face = switch (direction) {
					case DOWN -> new float[] {x0, y0, z0, x1, y0, z0, x1, y0, z1, x0, y0, z1};
					case UP -> new float[] {x0, y1, z0, x0, y1, z1, x1, y1, z1, x1, y1, z0};
					case NORTH -> new float[] {x0, y0, z0, x0, y1, z0, x1, y1, z0, x1, y0, z0};
					case SOUTH -> new float[] {x0, y0, z1, x1, y0, z1, x1, y1, z1, x0, y1, z1};
					case WEST -> new float[] {x0, y0, z0, x0, y0, z1, x0, y1, z1, x0, y1, z0};
					case EAST -> new float[] {x1, y0, z0, x1, y1, z0, x1, y1, z1, x1, y0, z1};
				};

				for (float value : face) {
					vertices.add(value);
				}
			}
		}

		float[] result = new float[vertices.size()];

		for (int i = 0; i < result.length; i++) {
			result[i] = vertices.get(i);
		}

		return result;
	}
}
