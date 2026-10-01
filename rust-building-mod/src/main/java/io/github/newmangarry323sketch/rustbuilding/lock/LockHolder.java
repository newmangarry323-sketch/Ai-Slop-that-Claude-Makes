package io.github.newmangarry323sketch.rustbuilding.lock;

import org.jspecify.annotations.Nullable;

import net.minecraft.core.BlockPos;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.level.block.DoorBlock;
import net.minecraft.world.level.block.entity.BlockEntity;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.state.properties.DoubleBlockHalf;

/** A block entity a code lock can be fitted to. */
public interface LockHolder {
	CodeLock lock();

	/** Where the lock lives: for a door, its bottom half. Implemented by {@link BlockEntity}. */
	BlockPos getBlockPos();

	/** Saves and tells clients after the lock changes. */
	void lockChanged();

	/** The lock holder for a clicked block, following a door's top half down to the bottom. */
	@Nullable
	static LockHolder find(BlockGetter level, BlockPos pos) {
		BlockPos target = pos;
		BlockState state = level.getBlockState(pos);

		if (state.getBlock() instanceof DoorBlock && state.getValue(DoorBlock.HALF) == DoubleBlockHalf.UPPER) {
			target = pos.below();
		}

		BlockEntity blockEntity = level.getBlockEntity(target);
		return blockEntity instanceof LockHolder holder ? holder : null;
	}
}
