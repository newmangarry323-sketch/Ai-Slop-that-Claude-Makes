package io.github.newmangarry323sketch.rustbuilding.block;

import org.jspecify.annotations.Nullable;

import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.Containers;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.InteractionResult;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.context.BlockPlaceContext;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.EntityBlock;
import net.minecraft.world.level.block.entity.BlockEntity;
import net.minecraft.world.level.block.entity.BlockEntityTicker;
import net.minecraft.world.level.block.entity.BlockEntityType;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.state.StateDefinition;
import net.minecraft.world.level.block.state.properties.BlockStateProperties;
import net.minecraft.world.level.block.state.properties.EnumProperty;
import net.minecraft.world.phys.BlockHitResult;

import io.github.newmangarry323sketch.rustbuilding.ClientBridge;
import io.github.newmangarry323sketch.rustbuilding.block.entity.ToolCupboardBlockEntity;
import io.github.newmangarry323sketch.rustbuilding.registry.ModBlockEntities;
import io.github.newmangarry323sketch.rustbuilding.registry.ModItems;

/**
 * The tool cupboard. Placing one authorises you on it; using it opens its privilege list and upkeep. It
 * pays the upkeep of the pieces around it from its storage as time passes.
 */
public class ToolCupboardBlock extends Block implements EntityBlock {
	public static final EnumProperty<Direction> FACING = BlockStateProperties.HORIZONTAL_FACING;

	public ToolCupboardBlock(Properties properties) {
		super(properties);
		this.registerDefaultState(this.stateDefinition.any().setValue(FACING, Direction.NORTH));
	}

	@Override
	protected void createBlockStateDefinition(StateDefinition.Builder<Block, BlockState> builder) {
		builder.add(FACING);
	}

	@Override
	public BlockState getStateForPlacement(BlockPlaceContext context) {
		return this.defaultBlockState().setValue(FACING, context.getHorizontalDirection().getOpposite());
	}

	@Nullable
	@Override
	public BlockEntity newBlockEntity(BlockPos pos, BlockState state) {
		return new ToolCupboardBlockEntity(pos, state);
	}

	/** On the server, the cupboard pays its upkeep as time passes. */
	@Override
	public <T extends BlockEntity> @Nullable BlockEntityTicker<T> getTicker(Level level, BlockState state, BlockEntityType<T> type) {
		if (level.isClientSide() || type != ModBlockEntities.TOOL_CUPBOARD) {
			return null;
		}

		return (tickerLevel, pos, tickerState, blockEntity) -> ((ToolCupboardBlockEntity) blockEntity).serverTick((ServerLevel) tickerLevel);
	}

	/** Whatever is left in the upkeep storage spills out when the cupboard goes. */
	@Override
	public void affectNeighborsAfterRemoval(BlockState state, ServerLevel level, BlockPos pos, boolean moved) {
		if (!level.getBlockState(pos).is(this) && level.getBlockEntity(pos) instanceof ToolCupboardBlockEntity cupboard) {
			Containers.dropContents(level, pos, cupboard.storage());
		}

		super.affectNeighborsAfterRemoval(state, level, pos, moved);
	}

	@Override
	public void setPlacedBy(Level level, BlockPos pos, BlockState state, @Nullable LivingEntity placer, ItemStack stack) {
		super.setPlacedBy(level, pos, state, placer, stack);

		if (!level.isClientSide() && placer instanceof Player player && level.getBlockEntity(pos) instanceof ToolCupboardBlockEntity cupboard) {
			cupboard.authorize(player);
		}
	}

	@Override
	protected InteractionResult useItemOn(ItemStack stack, BlockState state, Level level, BlockPos pos, Player player, InteractionHand hand, BlockHitResult hit) {
		// Let a code lock reach its item behaviour instead of opening the cupboard.
		if (stack.is(ModItems.CODE_LOCK)) {
			return InteractionResult.PASS;
		}

		return super.useItemOn(stack, state, level, pos, player, hand, hit);
	}

	@Override
	protected InteractionResult useWithoutItem(BlockState state, Level level, BlockPos pos, Player player, BlockHitResult hit) {
		if (level.isClientSide()) {
			ClientBridge.get().openToolCupboard(pos);
		}

		return InteractionResult.SUCCESS;
	}
}
