package io.github.newmangarry323sketch.rustbuilding.block.entity;

import net.minecraft.core.BlockPos;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.storage.ValueInput;
import net.minecraft.world.level.storage.ValueOutput;

import io.github.newmangarry323sketch.rustbuilding.lock.CodeLock;
import io.github.newmangarry323sketch.rustbuilding.lock.LockHolder;
import io.github.newmangarry323sketch.rustbuilding.registry.ModBlockEntities;

/** A garage door's code lock and which way it is rolling; only the door's controller block has one. */
public class GarageDoorBlockEntity extends SyncedBlockEntity implements LockHolder {
	private final CodeLock lock = new CodeLock();
	private boolean open;

	public GarageDoorBlockEntity(BlockPos pos, BlockState state) {
		super(ModBlockEntities.GARAGE_DOOR, pos, state);
	}

	/** Whether the door is open or opening (as opposed to closed or closing). */
	public boolean isOpen() {
		return this.open;
	}

	public void setOpen(boolean open) {
		this.open = open;
		this.setChanged();
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
		output.putBoolean("open", this.open);
		this.lock.save(output);
	}

	@Override
	protected void loadAdditional(ValueInput input) {
		super.loadAdditional(input);
		this.open = input.getBooleanOr("open", false);
		this.lock.load(input);
	}
}
