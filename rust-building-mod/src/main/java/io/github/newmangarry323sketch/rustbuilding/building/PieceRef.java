package io.github.newmangarry323sketch.rustbuilding.building;

import java.util.ArrayList;
import java.util.List;

import org.jspecify.annotations.Nullable;

import net.minecraft.core.BlockPos;
import net.minecraft.network.chat.Component;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.level.block.state.BlockState;

/** A piece that already exists in the world, identified by its place in the grid. */
public sealed interface PieceRef permits PieceRef.Slab, PieceRef.Wall, PieceRef.Stairs {
	/** One block that identifies the piece; damage is stored against it. */
	BlockPos anchor();

	/** The blocks that belong to this piece alone, not the line blocks and pillars it shares. */
	List<BlockPos> ownBlocks(BlockGetter level);

	PieceType type(BlockGetter level);

	/** Size used for costs: one unit per block, two per stair step. */
	default int units(BlockGetter level) {
		int blocks = this.ownBlocks(level).size();
		return this instanceof Stairs ? blocks * 2 : blocks;
	}

	@Nullable
	default BuildingTier tier(BlockGetter level) {
		BuildingTier best = null;

		for (BlockPos pos : this.ownBlocks(level)) {
			BuildingTier tier = Structure.tierOf(level.getBlockState(pos));

			if (tier != null && (best == null || tier.ordinal() < best.ordinal())) {
				best = tier;
			}
		}

		return best;
	}

	default Component describe(BlockGetter level) {
		BuildingTier tier = this.tier(level);
		Component piece = this.type(level).displayName();
		return tier == null ? piece : Component.translatable("piece.rustbuilding.described", tier.displayName(), piece);
	}

	/** A foundation or floor: the nine interior blocks of a cell at one slab level. */
	record Slab(int cellX, int cellZ, int y0) implements PieceRef {
		@Override
		public BlockPos anchor() {
			return Grid.slabAnchor(this.cellX, this.cellZ, this.y0);
		}

		@Override
		public List<BlockPos> ownBlocks(BlockGetter level) {
			List<BlockPos> own = new ArrayList<>(9);

			for (BlockPos pos : Grid.slabInterior(this.cellX, this.cellZ, this.y0)) {
				if (Structure.isKind(level, pos, BuildingKind.SLAB)) {
					own.add(pos);
				}
			}

			return own;
		}

		@Override
		public PieceType type(BlockGetter level) {
			return Structure.isFoundation(level, this.cellX, this.cellZ, this.y0) ? PieceType.FOUNDATION : PieceType.FLOOR;
		}
	}

	/** Any wall-like piece standing on one edge. */
	record Wall(Edge edge) implements PieceRef {
		@Override
		public BlockPos anchor() {
			return this.edge.anchor();
		}

		@Override
		public List<BlockPos> ownBlocks(BlockGetter level) {
			List<BlockPos> own = new ArrayList<>(9);

			for (int u = 0; u < 3; u++) {
				for (int h = 1; h <= Grid.WALL_HEIGHT; h++) {
					BlockPos pos = this.edge.pos(u, h);

					if (Structure.isKind(level, pos, BuildingKind.WALL)) {
						own.add(pos);
					}
				}
			}

			return own;
		}

		/** Recognises the piece from the blocks still standing; a damaged one reads as a plain wall. */
		@Override
		public PieceType type(BlockGetter level) {
			for (PieceType type : PieceType.values()) {
				if (type.category() != PieceType.Category.EDGE) {
					continue;
				}

				boolean matches = true;

				for (int u = 0; u < 3 && matches; u++) {
					for (int h = 1; h <= Grid.WALL_HEIGHT; h++) {
						if (type.edgeHas(u, h) != Structure.isKind(level, this.edge.pos(u, h), BuildingKind.WALL)) {
							matches = false;
							break;
						}
					}
				}

				if (matches) {
					return type;
				}
			}

			return PieceType.WALL;
		}
	}

	/** A staircase filling one cell from its slab up to the next storey's slab level. */
	record Stairs(int cellX, int cellZ, int y0) implements PieceRef {
		@Override
		public BlockPos anchor() {
			return Grid.slabAnchor(this.cellX, this.cellZ, this.y0 + 1);
		}

		@Override
		public List<BlockPos> ownBlocks(BlockGetter level) {
			List<BlockPos> own = new ArrayList<>(4);

			for (int h = 1; h <= Grid.STOREY; h++) {
				for (BlockPos pos : Grid.slabInterior(this.cellX, this.cellZ, this.y0 + h)) {
					BlockState state = level.getBlockState(pos);

					if (Structure.isStairs(state)) {
						own.add(pos);
					}
				}
			}

			return own;
		}

		@Override
		public PieceType type(BlockGetter level) {
			return PieceType.STAIRS;
		}
	}
}
