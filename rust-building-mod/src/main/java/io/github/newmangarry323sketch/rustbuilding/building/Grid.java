package io.github.newmangarry323sketch.rustbuilding.building;

import java.util.ArrayList;
import java.util.List;

import net.minecraft.core.BlockPos;
import net.minecraft.world.phys.Vec3;

/**
 * The building grid. Grid lines run every {@value #SPAN} blocks on X and Z (x % 4 == 0, z % 4 == 0);
 * a cell is the 3 x 3 interior between four lines. Walls stand on the lines, so neighbouring rooms share
 * them as they do in Rust, and a single foundation with four walls gives a 3 x 3 room - Rust's 3 m
 * foundation at one block per metre.
 *
 * <p>Vertically, a storey is the slab level {@code y0} (foundation or floor) plus three wall levels
 * above it; the next storey's floor sits at {@code y0 + 4}.
 */
public final class Grid {
	public static final int SPAN = 4;
	public static final int WALL_HEIGHT = 3;
	public static final int STOREY = WALL_HEIGHT + 1;

	private Grid() {
	}

	public static boolean onLine(int coordinate) {
		return Math.floorMod(coordinate, SPAN) == 0;
	}

	/** The cell whose interior or western/northern line contains this coordinate. */
	public static int cell(int coordinate) {
		return Math.floorDiv(coordinate, SPAN);
	}

	public static int lineOf(int cell) {
		return cell * SPAN;
	}

	public static boolean isInterior(BlockPos pos) {
		return !onLine(pos.getX()) && !onLine(pos.getZ());
	}

	/** The block at the centre of a cell's slab; a slab exists exactly when this block is a slab block. */
	public static BlockPos slabAnchor(int cellX, int cellZ, int y) {
		return new BlockPos(lineOf(cellX) + 2, y, lineOf(cellZ) + 2);
	}

	public static List<BlockPos> slabInterior(int cellX, int cellZ, int y) {
		List<BlockPos> positions = new ArrayList<>(9);
		int x0 = lineOf(cellX);
		int z0 = lineOf(cellZ);

		for (int dx = 1; dx <= 3; dx++) {
			for (int dz = 1; dz <= 3; dz++) {
				positions.add(new BlockPos(x0 + dx, y, z0 + dz));
			}
		}

		return positions;
	}

	/** The 16 line blocks around a cell at slab level: four 3-block edges and four corners. */
	public static List<BlockPos> slabBorder(int cellX, int cellZ, int y) {
		List<BlockPos> positions = new ArrayList<>(16);
		int x0 = lineOf(cellX);
		int z0 = lineOf(cellZ);

		for (int dx = 0; dx <= SPAN; dx++) {
			for (int dz = 0; dz <= SPAN; dz++) {
				if (dx == 0 || dx == SPAN || dz == 0 || dz == SPAN) {
					positions.add(new BlockPos(x0 + dx, y, z0 + dz));
				}
			}
		}

		return positions;
	}

	/**
	 * Cells whose slab border includes this line position: two for a position on one line, four for a
	 * grid corner. Each entry is {cellX, cellZ}.
	 */
	public static List<int[]> cellsSharing(BlockPos pos) {
		List<int[]> cells = new ArrayList<>(4);
		int[] xs = onLine(pos.getX()) ? new int[] {cell(pos.getX()) - 1, cell(pos.getX())} : new int[] {cell(pos.getX())};
		int[] zs = onLine(pos.getZ()) ? new int[] {cell(pos.getZ()) - 1, cell(pos.getZ())} : new int[] {cell(pos.getZ())};

		for (int x : xs) {
			for (int z : zs) {
				cells.add(new int[] {x, z});
			}
		}

		return cells;
	}

	/**
	 * The cell a viewer most likely means when pointing at this position: the cell containing it, or for
	 * a block on a shared line, the cell on the viewer's side of that line.
	 */
	public static int[] cellOnViewerSide(BlockPos pos, Vec3 viewer) {
		int cellX = onLine(pos.getX())
				? (viewer.x < pos.getX() + 0.5 ? cell(pos.getX()) - 1 : cell(pos.getX()))
				: cell(pos.getX());
		int cellZ = onLine(pos.getZ())
				? (viewer.z < pos.getZ() + 0.5 ? cell(pos.getZ()) - 1 : cell(pos.getZ()))
				: cell(pos.getZ());
		return new int[] {cellX, cellZ};
	}

	/** The four edges around a cell for the storey whose slab is at {@code y0}. */
	public static List<Edge> edgesOf(int cellX, int cellZ, int y0) {
		return List.of(
				Edge.alongX(lineOf(cellZ), cellX, y0),
				Edge.alongX(lineOf(cellZ + 1), cellX, y0),
				Edge.alongZ(lineOf(cellX), cellZ, y0),
				Edge.alongZ(lineOf(cellX + 1), cellZ, y0)
		);
	}
}
