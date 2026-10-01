package io.github.newmangarry323sketch.rustbuilding.block.entity;

import net.minecraft.core.BlockPos;
import net.minecraft.core.HolderLookup;
import net.minecraft.nbt.CompoundTag;
import net.minecraft.network.protocol.Packet;
import net.minecraft.network.protocol.game.ClientGamePacketListener;
import net.minecraft.network.protocol.game.ClientboundBlockEntityDataPacket;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.entity.BlockEntity;
import net.minecraft.world.level.block.entity.BlockEntityType;
import net.minecraft.world.level.block.state.BlockState;

import io.github.newmangarry323sketch.rustbuilding.lock.CodeLock;

/**
 * A block entity whose state clients need (lock state, access lists) so they can show the right screen
 * without a round trip. The lock code is removed before anything is sent.
 */
public abstract class SyncedBlockEntity extends BlockEntity {
	protected SyncedBlockEntity(BlockEntityType<?> type, BlockPos pos, BlockState state) {
		super(type, pos, state);
	}

	/** Marks the data dirty and pushes it to everyone who can see the block. */
	protected void sync() {
		this.setChanged();

		if (this.level != null && !this.level.isClientSide()) {
			BlockState state = this.getBlockState();
			this.level.sendBlockUpdated(this.worldPosition, state, state, Block.UPDATE_CLIENTS);
		}
	}

	@Override
	public CompoundTag getUpdateTag(HolderLookup.Provider registries) {
		CompoundTag tag = this.saveWithoutMetadata(registries);
		tag.getCompound(CodeLock.KEY).ifPresent(lock -> lock.remove(CodeLock.CODE_KEY));
		return tag;
	}

	@Override
	public Packet<ClientGamePacketListener> getUpdatePacket() {
		return ClientboundBlockEntityDataPacket.create(this);
	}
}
