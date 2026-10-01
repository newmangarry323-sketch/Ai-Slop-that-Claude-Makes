package io.github.newmangarry323sketch.rustbuilding.test;

import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.DoorBlock;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.state.properties.DoubleBlockHalf;

import io.github.newmangarry323sketch.rustbuilding.building.BuildingOps;
import io.github.newmangarry323sketch.rustbuilding.building.BuildingTier;
import io.github.newmangarry323sketch.rustbuilding.building.Edge;
import io.github.newmangarry323sketch.rustbuilding.building.Grid;
import io.github.newmangarry323sketch.rustbuilding.building.PiecePlanner;
import io.github.newmangarry323sketch.rustbuilding.building.PieceRef;
import io.github.newmangarry323sketch.rustbuilding.building.PieceType;
import io.github.newmangarry323sketch.rustbuilding.building.PlannedPiece;
import io.github.newmangarry323sketch.rustbuilding.registry.ModBlocks;

/** Building through the same planner and operations the items use, for tests and screenshots. */
final class TestBuilds {
	private TestBuilds() {
	}

	/** Plans and places a piece, failing loudly if the planner refuses it. */
	static PlannedPiece place(Level level, PieceType type, PiecePlanner.Target target) {
		PlannedPiece plan = PiecePlanner.planAt(level, null, type, target);

		if (!plan.valid()) {
			throw new AssertionError("Could not place " + type + " at " + target + ": " + plan.problem().getString());
		}

		BuildingOps.place(level, plan);
		return plan;
	}

	static PlannedPiece slab(Level level, PieceType type, int cellX, int cellZ, int y) {
		return place(level, type, new PiecePlanner.SlabTarget(cellX, cellZ, y));
	}

	static PlannedPiece wall(Level level, PieceType type, Edge edge) {
		return place(level, type, new PiecePlanner.EdgeTarget(edge));
	}

	static Edge north(int cellX, int cellZ, int y0) {
		return Edge.alongX(Grid.lineOf(cellZ), cellX, y0);
	}

	static Edge south(int cellX, int cellZ, int y0) {
		return Edge.alongX(Grid.lineOf(cellZ + 1), cellX, y0);
	}

	static Edge west(int cellX, int cellZ, int y0) {
		return Edge.alongZ(Grid.lineOf(cellX), cellZ, y0);
	}

	static Edge east(int cellX, int cellZ, int y0) {
		return Edge.alongZ(Grid.lineOf(cellX + 1), cellZ, y0);
	}

	/**
	 * A two-by-two base showing every grade: armored and sheet metal foundations, stone and wood walls
	 * with a doorway (and a sheet metal door) and a window, twig on the first floor, and a staircase.
	 *
	 * @param y the slab level of the foundations, one above the ground
	 */
	static void demoBase(Level level, int cellX, int cellZ, int y) {
		for (int dx = 0; dx < 2; dx++) {
			for (int dz = 0; dz < 2; dz++) {
				slab(level, PieceType.FOUNDATION, cellX + dx, cellZ + dz, y);
			}
		}

		// Outer walls of the ground floor.
		wall(level, PieceType.DOORWAY, south(cellX, cellZ + 1, y));
		wall(level, PieceType.WINDOW, south(cellX + 1, cellZ + 1, y));
		wall(level, PieceType.WALL, north(cellX, cellZ, y));
		wall(level, PieceType.WALL, north(cellX + 1, cellZ, y));
		wall(level, PieceType.WALL, west(cellX, cellZ, y));
		wall(level, PieceType.WINDOW, west(cellX, cellZ + 1, y));
		wall(level, PieceType.WALL, east(cellX + 1, cellZ, y));
		wall(level, PieceType.HALF_WALL, east(cellX + 1, cellZ + 1, y));

		place(level, PieceType.STAIRS, new PiecePlanner.StairsTarget(cellX, cellZ, y, Direction.NORTH));

		// A first floor over three of the four cells (the stairs come up through the fourth).
		int upper = y + Grid.STOREY;
		slab(level, PieceType.FLOOR, cellX + 1, cellZ, upper);
		slab(level, PieceType.FLOOR, cellX + 1, cellZ + 1, upper);
		slab(level, PieceType.FLOOR, cellX, cellZ + 1, upper);
		wall(level, PieceType.LOW_WALL, south(cellX + 1, cellZ + 1, upper));
		wall(level, PieceType.LOW_WALL, east(cellX + 1, cellZ + 1, upper));

		// Show off the grades.
		BuildingOps.upgrade(level, new PieceRef.Slab(cellX, cellZ + 1, y), BuildingTier.ARMORED);
		BuildingOps.upgrade(level, new PieceRef.Slab(cellX + 1, cellZ + 1, y), BuildingTier.METAL);
		BuildingOps.upgrade(level, new PieceRef.Wall(south(cellX, cellZ + 1, y)), BuildingTier.STONE);
		BuildingOps.upgrade(level, new PieceRef.Wall(south(cellX + 1, cellZ + 1, y)), BuildingTier.WOOD);
		BuildingOps.upgrade(level, new PieceRef.Wall(east(cellX + 1, cellZ + 1, y)), BuildingTier.METAL);
		BuildingOps.upgrade(level, new PieceRef.Wall(west(cellX, cellZ + 1, y)), BuildingTier.ARMORED);
		BuildingOps.upgrade(level, new PieceRef.Stairs(cellX, cellZ, y), BuildingTier.WOOD);

		// A sheet metal door in the doorway.
		BlockPos doorway = south(cellX, cellZ + 1, y).pos(1, 1);
		BlockState door = ModBlocks.SHEET_METAL_DOOR.defaultBlockState().setValue(DoorBlock.FACING, Direction.NORTH);
		level.setBlock(doorway, door.setValue(DoorBlock.HALF, DoubleBlockHalf.LOWER), Block.UPDATE_ALL);
		level.setBlock(doorway.above(), door.setValue(DoorBlock.HALF, DoubleBlockHalf.UPPER), Block.UPDATE_ALL);
	}

	/** Absolute centre of the slab of a cell, for aiming the camera. */
	static BlockPos centre(int cellX, int cellZ, int y) {
		return Grid.slabAnchor(cellX, cellZ, y);
	}
}
