package io.github.newmangarry323sketch.rustbuilding.block;

import java.util.List;
import java.util.function.BiConsumer;

import org.jspecify.annotations.Nullable;

import net.minecraft.core.BlockPos;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.InteractionResult;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.level.Explosion;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.DoorBlock;
import net.minecraft.world.level.block.EntityBlock;
import net.minecraft.world.level.block.entity.BlockEntity;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.state.properties.BlockSetType;
import net.minecraft.world.level.block.state.properties.DoubleBlockHalf;
import net.minecraft.world.level.redstone.Orientation;
import net.minecraft.world.phys.BlockHitResult;

import io.github.newmangarry323sketch.rustbuilding.ClientBridge;
import io.github.newmangarry323sketch.rustbuilding.LockScreenMode;
import io.github.newmangarry323sketch.rustbuilding.block.entity.RustDoorBlockEntity;
import io.github.newmangarry323sketch.rustbuilding.lock.CodeLock;
import io.github.newmangarry323sketch.rustbuilding.lock.LockHolder;
import io.github.newmangarry323sketch.rustbuilding.raid.RaidDamage;
import io.github.newmangarry323sketch.rustbuilding.registry.ModItems;

/**
 * A metal door that opens by hand, takes a code lock, and has health like a building piece. A locked
 * door ignores redstone, so a button outside cannot open it.
 */
public class RustDoorBlock extends DoorBlock implements EntityBlock {
	private final int maxHealth;

	public RustDoorBlock(BlockSetType type, int maxHealth, Properties properties) {
		super(type, properties);
		this.maxHealth = maxHealth;
	}

	public int maxHealth() {
		return this.maxHealth;
	}

	public static BlockPos lowerHalf(BlockPos pos, BlockState state) {
		return state.getValue(HALF) == DoubleBlockHalf.UPPER ? pos.below() : pos;
	}

	@Nullable
	@Override
	public BlockEntity newBlockEntity(BlockPos pos, BlockState state) {
		return state.getValue(HALF) == DoubleBlockHalf.LOWER ? new RustDoorBlockEntity(pos, state) : null;
	}

	@Override
	protected InteractionResult useItemOn(ItemStack stack, BlockState state, Level level, BlockPos pos, Player player, InteractionHand hand, BlockHitResult hit) {
		if (stack.is(ModItems.CODE_LOCK)) {
			return InteractionResult.PASS;
		}

		return super.useItemOn(stack, state, level, pos, player, hand, hit);
	}

	@Override
	protected InteractionResult useWithoutItem(BlockState state, Level level, BlockPos pos, Player player, BlockHitResult hit) {
		LockHolder holder = LockHolder.find(level, pos);

		if (holder != null && holder.lock().isLocked()) {
			CodeLock lock = holder.lock();
			BlockPos lockPos = holder.getBlockPos();

			if (player.isShiftKeyDown() && lock.isOwner(player)) {
				if (level.isClientSide()) {
					ClientBridge.get().openCodeLock(lockPos, LockScreenMode.OWNER);
				}

				return InteractionResult.SUCCESS;
			}

			if (!lock.canAccess(player)) {
				if (level.isClientSide()) {
					ClientBridge.get().openCodeLock(lockPos, LockScreenMode.ENTER);
				}

				return InteractionResult.SUCCESS;
			}
		}

		return super.useWithoutItem(state, level, pos, player, hit);
	}

	@Override
	protected void neighborChanged(BlockState state, Level level, BlockPos pos, Block neighborBlock, @Nullable Orientation orientation, boolean movedByPiston) {
		LockHolder holder = LockHolder.find(level, pos);

		if (holder != null && holder.lock().isLocked()) {
			return;
		}

		super.neighborChanged(state, level, pos, neighborBlock, orientation, movedByPiston);
	}

	@Override
	protected void onExplosionHit(BlockState state, ServerLevel level, BlockPos pos, Explosion explosion, BiConsumer<ItemStack, BlockPos> dropConsumer) {
		BlockPos lower = lowerHalf(pos, state);

		if (RaidDamage.damageDoor(level, lower, List.of(lower, lower.above()), this.maxHealth, explosion)) {
			// Removing the bottom half takes the top half with it; neither drops anything.
			level.setBlock(lower, Blocks.AIR.defaultBlockState(), Block.UPDATE_ALL);
		}
	}
}
