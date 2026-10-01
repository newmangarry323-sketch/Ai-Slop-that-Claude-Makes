package io.github.newmangarry323sketch.rustbuilding.block.entity;

import net.minecraft.core.BlockPos;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.storage.ValueInput;
import net.minecraft.world.level.storage.ValueOutput;

import io.github.newmangarry323sketch.rustbuilding.lock.CodeLock;
import io.github.newmangarry323sketch.rustbuilding.lock.LockHolder;
import io.github.newmangarry323sketch.rustbuilding.registry.ModBlockEntities;

/** The code lock of a Rust door; only the bottom half of the door has one. */
public class RustDoorBlockEntity extends SyncedBlockEntity implements LockHolder {
	private final CodeLock lock = new CodeLock();

	public RustDoorBlockEntity(BlockPos pos, BlockState state) {
		super(ModBlockEntities.RUST_DOOR, pos, state);
	}

	@Override
	public CodeLock lock() {
		return this.lock;
	}

	@Override
	public void lockChanged() {
		this.sync();
	}

	@Override
	protected void saveAdditional(ValueOutput output) {
		super.saveAdditional(output);
		this.lock.save(output);
	}

	@Override
	protected void loadAdditional(ValueInput input) {
		super.loadAdditional(input);
		this.lock.load(input);
	}
}
