package io.github.newmangarry323sketch.rustbuilding.building;

import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;

/**
 * One wall slot: the three blocks of a grid line between two corners, for one storey.
 *
 * @param axis the direction the wall runs in ({@code X} or {@code Z})
 * @param line the fixed coordinate of the grid line (z for an X wall, x for a Z wall)
 * @param segment the cell index along the wall
 * @param y0 the slab level the wall stands on
 */
public record Edge(Direction.Axis axis, int line, int segment, int y0) {
	public static Edge alongX(int lineZ, int cellX, int y0) {
		return new Edge(Direction.Axis.X, lineZ, cellX, y0);
	}

	public static Edge alongZ(int lineX, int cellZ, int y0) {
		return new Edge(Direction.Axis.Z, lineX, cellZ, y0);
	}

	/** Column {@code u} (0-2) along the wall at height {@code h} above the slab (0 is the slab itself). */
	public BlockPos pos(int u, int h) {
		int along = Grid.lineOf(this.segment) + 1 + u;
		return this.axis == Direction.Axis.X
				? new BlockPos(along, this.y0 + h, this.line)
				: new BlockPos(this.line, this.y0 + h, along);
	}

	/** The grid corner at the low ({@code end == 0}) or high ({@code end == 1}) end of the wall. */
	public BlockPos corner(int end, int h) {
		int along = Grid.lineOf(this.segment + end);
		return this.axis == Direction.Axis.X
				? new BlockPos(along, this.y0 + h, this.line)
				: new BlockPos(this.line, this.y0 + h, along);
	}

	/** The block every wall-like piece keeps: bottom of the first column. Used to key damage. */
	public BlockPos anchor() {
		return this.pos(0, 1);
	}

	/** The two cells this wall separates, each as {cellX, cellZ}. */
	public int[][] cells() {
		int side = Grid.cell(this.line);
		return this.axis == Direction.Axis.X
				? new int[][] {{this.segment, side - 1}, {this.segment, side}}
				: new int[][] {{side - 1, this.segment}, {side, this.segment}};
	}
}
