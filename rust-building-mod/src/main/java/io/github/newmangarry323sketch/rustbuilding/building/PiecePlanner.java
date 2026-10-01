package io.github.newmangarry323sketch.rustbuilding.building;

import java.util.ArrayList;
import java.util.List;

import org.jspecify.annotations.Nullable;

import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.network.chat.Component;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.level.ClipContext;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.block.StairBlock;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.state.properties.Half;
import net.minecraft.world.level.block.state.properties.StairsShape;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.HitResult;
import net.minecraft.world.phys.Vec3;
import net.minecraft.world.phys.shapes.CollisionContext;

import io.github.newmangarry323sketch.rustbuilding.block.BuildingBlock;
import io.github.newmangarry323sketch.rustbuilding.privilege.BuildingPrivilege;
import io.github.newmangarry323sketch.rustbuilding.registry.ModBlocks;

/**
 * Turns "the player is looking here with this piece selected" into a {@link PlannedPiece}. The client
 * runs this every tick to draw the preview; the server runs it again when the plan is used, so what you
 * see is what gets checked.
 *
 * <p>Where to aim:
 * <ul>
 * <li>Foundation - the ground, or the side of another foundation to continue the grid.</li>
 * <li>Wall, doorway, window, half and low wall - the top of a foundation or floor, near the edge.</li>
 * <li>Floor - the top of a wall (it goes on your side of it), the side of another floor, or the floor
 * of the room it should cover.</li>
 * <li>Stairs - the floor of the cell; they climb in the direction you face.</li>
 * </ul>
 */
public final class PiecePlanner {
	/** How far a foundation may stand above the ground before it needs a foundation next to it. */
	public static final int MAX_LEG = 3;
	private static final double EXTRA_REACH = 2.0;

	private PiecePlanner() {
	}

	public sealed interface Target permits SlabTarget, EdgeTarget, StairsTarget {
	}

	public record SlabTarget(int cellX, int cellZ, int y0) implements Target {
	}

	public record EdgeTarget(Edge edge) implements Target {
	}

	public record StairsTarget(int cellX, int cellZ, int y0, Direction forward) implements Target {
	}

	@Nullable
	public static PlannedPiece plan(Level level, Player player, PieceType type) {
		BlockHitResult hit = raycast(level, player);

		if (hit == null) {
			return null;
		}

		Target target = target(level, player, type, hit);
		return target == null ? null : planAt(level, player, type, target);
	}

	@Nullable
	public static BlockHitResult raycast(Level level, Player player) {
		double reach = Math.max(player.blockInteractionRange(), 4.5) + EXTRA_REACH;
		Vec3 eye = player.getEyePosition();
		Vec3 end = eye.add(player.getViewVector(1.0F).scale(reach));
		BlockHitResult hit = level.clip(new ClipContext(eye, end, ClipContext.Block.OUTLINE, ClipContext.Fluid.NONE, player));
		return hit.getType() == HitResult.Type.MISS ? null : hit;
	}

	@Nullable
	public static Target target(Level level, Player player, PieceType type, BlockHitResult hit) {
		BlockPos hitPos = hit.getBlockPos();
		BlockState hitState = level.getBlockState(hitPos);
		Direction face = hit.getDirection();
		Vec3 eye = player.getEyePosition();
		BuildingKind kind = Structure.kindOf(hitState);

		return switch (type.category()) {
			case SLAB -> type == PieceType.FOUNDATION
					? foundationTarget(level, hitPos, hitState, face, kind)
					: floorTarget(level, hitPos, hitState, face, kind, eye);
			case EDGE -> {
				if (kind != BuildingKind.SLAB || face != Direction.UP) {
					yield null;
				}

				yield new EdgeTarget(nearestEdge(hitPos, hit.getLocation()));
			}
			case STAIRS -> {
				if (kind != BuildingKind.SLAB || face != Direction.UP) {
					yield null;
				}

				int[] cell = Grid.cellOnViewerSide(hitPos, eye);
				yield new StairsTarget(cell[0], cell[1], hitPos.getY(), player.getDirection());
			}
		};
	}

	@Nullable
	private static Target foundationTarget(Level level, BlockPos hitPos, BlockState hitState, Direction face, @Nullable BuildingKind kind) {
		if (kind == BuildingKind.SLAB || kind == BuildingKind.SUPPORT) {
			// Continue the grid from the side of an existing foundation, at the same height.
			if (face.getAxis().isVertical()) {
				return null;
			}

			int y0 = Structure.storeyOf(level, hitPos, hitState);

			if (y0 == Structure.UNKNOWN) {
				return null;
			}

			BlockPos next = hitPos.relative(face);
			return new SlabTarget(Grid.cell(next.getX()), Grid.cell(next.getZ()), y0);
		}

		if (Structure.isPiece(hitState)) {
			return null;
		}

		BlockPos base = hitState.canBeReplaced() ? hitPos : hitPos.relative(face);
		return new SlabTarget(Grid.cell(base.getX()), Grid.cell(base.getZ()), base.getY());
	}

	@Nullable
	private static Target floorTarget(Level level, BlockPos hitPos, BlockState hitState, Direction face, @Nullable BuildingKind kind, Vec3 eye) {
		if (kind == null) {
			return null;
		}

		int y0 = Structure.storeyOf(level, hitPos, hitState);

		if (y0 == Structure.UNKNOWN) {
			return null;
		}

		if (kind == BuildingKind.WALL) {
			// On top of the wall, over the room on the player's side of it.
			int[] cell = Grid.cellOnViewerSide(hitPos, eye);
			return new SlabTarget(cell[0], cell[1], y0 + Grid.STOREY);
		}

		if (face.getAxis().isHorizontal()) {
			BlockPos next = hitPos.relative(face);
			return new SlabTarget(Grid.cell(next.getX()), Grid.cell(next.getZ()), y0);
		}

		if (face == Direction.UP) {
			// Looking at a floor: put the ceiling over that room.
			int[] cell = Grid.cellOnViewerSide(hitPos, eye);
			return new SlabTarget(cell[0], cell[1], y0 + Grid.STOREY);
		}

		return null;
	}

	/** The edge of the cell under the hit point that the hit point is closest to. */
	public static Edge nearestEdge(BlockPos hitPos, Vec3 location) {
		int cellX = Grid.cell(hitPos.getX());
		int cellZ = Grid.cell(hitPos.getZ());
		double localX = location.x - Grid.lineOf(cellX);
		double localZ = location.z - Grid.lineOf(cellZ);
		// Distances to the centres of the four line blocks around the cell.
		double west = Math.abs(localX - 0.5);
		double east = Math.abs(Grid.SPAN + 0.5 - localX);
		double north = Math.abs(localZ - 0.5);
		double south = Math.abs(Grid.SPAN + 0.5 - localZ);
		double min = Math.min(Math.min(west, east), Math.min(north, south));
		int y0 = hitPos.getY();

		if (min == west) {
			return Edge.alongZ(Grid.lineOf(cellX), cellZ, y0);
		}

		if (min == east) {
			return Edge.alongZ(Grid.lineOf(cellX + 1), cellZ, y0);
		}

		if (min == north) {
			return Edge.alongX(Grid.lineOf(cellZ), cellX, y0);
		}

		return Edge.alongX(Grid.lineOf(cellZ + 1), cellX, y0);
	}

	/**
	 * Plans a piece at an explicit grid position. {@code player} may be null (tests, commands), in which
	 * case building privilege is not checked.
	 */
	public static PlannedPiece planAt(Level level, @Nullable Player player, PieceType type, Target target) {
		Builder builder = new Builder();

		PieceRef ref = switch (target) {
			case SlabTarget slab -> {
				planSlab(level, type, slab, builder);
				yield new PieceRef.Slab(slab.cellX(), slab.cellZ(), slab.y0());
			}
			case EdgeTarget edge -> {
				planEdge(level, type, edge.edge(), builder);
				yield new PieceRef.Wall(edge.edge());
			}
			case StairsTarget stairs -> {
				planStairs(level, stairs, builder);
				yield new PieceRef.Stairs(stairs.cellX(), stairs.cellZ(), stairs.y0());
			}
		};

		if (builder.problem == null) {
			for (int i = 0; i < builder.positions.size(); i++) {
				BlockPos pos = builder.positions.get(i);

				if (level.isOutsideBuildHeight(pos)) {
					builder.fail("out_of_world");
					break;
				}

				if (!level.isUnobstructed(builder.states.get(i), pos, CollisionContext.empty())) {
					builder.fail("obstructed");
					break;
				}
			}
		}

		if (builder.problem == null && player != null && !BuildingPrivilege.canBuildAll(level, player, builder.positions)) {
			builder.fail("building_blocked");
		}

		return new PlannedPiece(type, ref, List.copyOf(builder.positions), List.copyOf(builder.states), builder.units, builder.problem);
	}

	private static void planSlab(Level level, PieceType type, SlabTarget target, Builder builder) {
		BlockState slab = twig(BuildingKind.SLAB);

		for (BlockPos pos : Grid.slabInterior(target.cellX(), target.cellZ(), target.y0())) {
			BlockState existing = level.getBlockState(pos);

			if (!existing.canBeReplaced()) {
				builder.fail(Structure.isPiece(existing) ? "occupied" : "blocked");
			}

			builder.add(pos, slab);
		}

		for (BlockPos pos : Grid.slabBorder(target.cellX(), target.cellZ(), target.y0())) {
			BlockState existing = level.getBlockState(pos);

			if (Structure.kindOf(existing) == BuildingKind.SLAB) {
				continue; // already there, shared with a neighbouring foundation or floor
			}

			if (!existing.canBeReplaced()) {
				builder.fail(Structure.isPiece(existing) ? "occupied" : "blocked");
			}

			builder.add(pos, slab);
		}

		builder.units = 9;

		if (type == PieceType.FOUNDATION) {
			BlockState leg = twig(BuildingKind.SUPPORT);
			int slabBlocks = builder.positions.size();

			for (int i = 0; i < slabBlocks; i++) {
				BlockPos pos = builder.positions.get(i);
				int depth = 0;

				while (depth <= MAX_LEG && Structure.isFree(level, pos.below(depth + 1))) {
					depth++;
				}

				if (depth > MAX_LEG) {
					builder.fail("too_high");
					continue;
				}

				for (int d = 1; d <= depth; d++) {
					builder.add(pos.below(d), leg);
				}
			}
		} else if (!floorSupported(level, target.cellX(), target.cellZ(), target.y0())) {
			builder.fail("floor_needs_support");
		}
	}

	private static void planEdge(Level level, PieceType type, Edge edge, Builder builder) {
		if (!Structure.edgeSupported(level, edge)) {
			builder.fail("wall_needs_support");
		}

		BlockState wall = twig(BuildingKind.WALL);
		int units = 0;

		for (int u = 0; u < 3; u++) {
			for (int h = 1; h <= Grid.WALL_HEIGHT; h++) {
				if (!type.edgeHas(u, h)) {
					continue;
				}

				BlockPos pos = edge.pos(u, h);
				BlockState existing = level.getBlockState(pos);

				if (!existing.canBeReplaced()) {
					builder.fail(Structure.isPiece(existing) ? "wall_exists" : "blocked");
				}

				builder.add(pos, wall);
				units++;
			}
		}

		// Corner pillars where this wall meets the next one; shared, so only add the missing ones.
		for (int h = 1; h <= Grid.WALL_HEIGHT; h++) {
			for (int end = 0; end <= 1; end++) {
				if (!type.edgeHas(end * 2, h)) {
					continue;
				}

				BlockPos corner = edge.corner(end, h);
				BlockState existing = level.getBlockState(corner);

				if (Structure.kindOf(existing) == BuildingKind.WALL) {
					continue;
				}

				if (!existing.canBeReplaced()) {
					builder.fail("blocked");
				}

				builder.add(corner, wall);
			}
		}

		builder.units = units;
	}

	private static void planStairs(Level level, StairsTarget target, Builder builder) {
		if (!Structure.slabExists(level, target.cellX(), target.cellZ(), target.y0())) {
			builder.fail("stairs_need_floor");
		}

		Direction forward = target.forward();
		Direction right = forward.getClockWise();
		BlockPos centre = Grid.slabAnchor(target.cellX(), target.cellZ(), target.y0());
		// {u, v, height}: three steps straight ahead up the middle, then a turn to the right onto the
		// fourth, which is level with the next storey's floor.
		int[][] steps = {{0, 1, 1}, {1, 1, 2}, {2, 1, 3}, {2, 2, 4}};

		for (int i = 0; i < steps.length; i++) {
			int[] step = steps[i];
			BlockPos pos = centre.relative(forward, step[0] - 1).relative(right, step[1] - 1).above(step[2]);
			Direction facing = i == steps.length - 1 ? right : forward;
			BlockState state = ModBlocks.stairs(BuildingTier.TWIG).defaultBlockState()
					.setValue(StairBlock.FACING, facing)
					.setValue(StairBlock.HALF, Half.BOTTOM)
					.setValue(StairBlock.SHAPE, StairsShape.STRAIGHT);

			if (!Structure.isFree(level, pos)) {
				builder.fail(Structure.isPiece(level.getBlockState(pos)) ? "occupied" : "blocked");
			}

			builder.add(pos, state);
		}

		builder.units = steps.length * 2;
	}

	/** A floor rests on at least one wall of the storey below, or on a floor beside it. */
	public static boolean floorSupported(BlockGetter level, int cellX, int cellZ, int y) {
		for (Edge edge : Grid.edgesOf(cellX, cellZ, y - Grid.STOREY)) {
			if (Structure.hasWall(level, edge)) {
				return true;
			}
		}

		return Structure.slabExists(level, cellX - 1, cellZ, y)
				|| Structure.slabExists(level, cellX + 1, cellZ, y)
				|| Structure.slabExists(level, cellX, cellZ - 1, y)
				|| Structure.slabExists(level, cellX, cellZ + 1, y);
	}

	private static BlockState twig(BuildingKind kind) {
		return ModBlocks.building(BuildingTier.TWIG).defaultBlockState().setValue(BuildingBlock.KIND, kind);
	}

	private static final class Builder {
		final List<BlockPos> positions = new ArrayList<>();
		final List<BlockState> states = new ArrayList<>();
		int units;
		@Nullable
		Component problem;

		void add(BlockPos pos, BlockState state) {
			this.positions.add(pos);
			this.states.add(state);
		}

		void fail(String reason) {
			if (this.problem == null) {
				this.problem = Component.translatable("message.rustbuilding." + reason);
			}
		}
	}
}
