package io.github.newmangarry323sketch.rustbuilding.lock;

import org.jspecify.annotations.Nullable;

import net.minecraft.core.BlockPos;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.level.block.DoorBlock;
import net.minecraft.world.level.block.entity.BlockEntity;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.state.properties.DoubleBlockHalf;

import io.github.newmangarry323sketch.rustbuilding.block.GarageDoorBlock;

/** A block entity a code lock can be fitted to. */
public interface LockHolder {
	CodeLock lock();

	/** Where the lock lives: a door's bottom half, a garage door's controller. Implemented by {@link BlockEntity}. */
	BlockPos getBlockPos();

	/** Saves and tells clients after the lock changes. */
	void lockChanged();

	/** The lock holder for a clicked block: a door's bottom half, a garage door's controller, or the block itself. */
	@Nullable
	static LockHolder find(BlockGetter level, BlockPos pos) {
		BlockPos target = pos;
		BlockState state = level.getBlockState(pos);

		if (state.getBlock() instanceof DoorBlock && state.getValue(DoorBlock.HALF) == DoubleBlockHalf.UPPER) {
			target = pos.below();
		} else if (state.getBlock() instanceof GarageDoorBlock) {
			target = GarageDoorBlock.controller(pos, state);
		}

		BlockEntity blockEntity = level.getBlockEntity(target);
		return blockEntity instanceof LockHolder holder ? holder : null;
	}
}
