package io.github.newmangarry323sketch.rustbuilding.block;

import java.util.ArrayList;
import java.util.List;
import java.util.function.BiConsumer;

import org.jspecify.annotations.Nullable;

import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.sounds.SoundEvents;
import net.minecraft.sounds.SoundSource;
import net.minecraft.util.RandomSource;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.InteractionResult;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.level.Explosion;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.EntityBlock;
import net.minecraft.world.level.block.entity.BlockEntity;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.state.StateDefinition;
import net.minecraft.world.level.block.state.properties.BlockStateProperties;
import net.minecraft.world.level.block.state.properties.BooleanProperty;
import net.minecraft.world.level.block.state.properties.DoubleBlockHalf;
import net.minecraft.world.level.block.state.properties.EnumProperty;
import net.minecraft.world.level.block.state.properties.IntegerProperty;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.shapes.CollisionContext;
import net.minecraft.world.phys.shapes.Shapes;
import net.minecraft.world.phys.shapes.VoxelShape;

import io.github.newmangarry323sketch.rustbuilding.ClientBridge;
import io.github.newmangarry323sketch.rustbuilding.LockScreenMode;
import io.github.newmangarry323sketch.rustbuilding.block.entity.GarageDoorBlockEntity;
import io.github.newmangarry323sketch.rustbuilding.building.Edge;
import io.github.newmangarry323sketch.rustbuilding.building.PieceRef;
import io.github.newmangarry323sketch.rustbuilding.building.PieceType;
import io.github.newmangarry323sketch.rustbuilding.building.Structure;
import io.github.newmangarry323sketch.rustbuilding.lock.CodeLock;
import io.github.newmangarry323sketch.rustbuilding.raid.PieceDamage;
import io.github.newmangarry323sketch.rustbuilding.raid.RaidDamage;
import io.github.newmangarry323sketch.rustbuilding.registry.ModItems;

/**
 * Rust's garage door: a roll-up metal door that fills the 3 x 2 opening of a wall frame. It is six
 * blocks; the top block of the first column is the controller, which holds the code lock and is where
 * explosion damage is counted (600 health, Rust's figure). Opening rolls the door up into the frame's
 * beam a row at a time, and it will not come down on someone standing in the way.
 */
public class GarageDoorBlock extends Block implements EntityBlock {
	public static final EnumProperty<Direction.Axis> AXIS = BlockStateProperties.HORIZONTAL_AXIS;
	public static final IntegerProperty COLUMN = IntegerProperty.create("column", 0, 2);
	public static final EnumProperty<DoubleBlockHalf> HALF = BlockStateProperties.DOUBLE_BLOCK_HALF;
	public static final BooleanProperty OPEN = BlockStateProperties.OPEN;

	/** Rust's garage door has 600 health: three TNT, between the sheet metal (250) and armored (800) doors. */
	public static final int MAX_HEALTH = 600;
	/** Ticks between the two rows moving. */
	public static final int ROLL_TICKS = 8;

	private static final VoxelShape PANEL_X = Block.box(0.0, 0.0, 6.0, 16.0, 16.0, 10.0);
	private static final VoxelShape PANEL_Z = Block.box(6.0, 0.0, 0.0, 10.0, 16.0, 16.0);
	// Rolled up, only the door's bottom bar shows, just under the beam; low enough to walk under.
	private static final VoxelShape ROLLED_X = Block.box(0.0, 14.0, 6.0, 16.0, 16.0, 10.0);
	private static final VoxelShape ROLLED_Z = Block.box(6.0, 14.0, 0.0, 10.0, 16.0, 16.0);

	public GarageDoorBlock(Properties properties) {
		super(properties);
		this.registerDefaultState(this.stateDefinition.any()
				.setValue(AXIS, Direction.Axis.X)
				.setValue(COLUMN, 0)
				.setValue(HALF, DoubleBlockHalf.LOWER)
				.setValue(OPEN, false));
	}

	@Override
	protected void createBlockStateDefinition(StateDefinition.Builder<Block, BlockState> builder) {
		builder.add(AXIS, COLUMN, HALF, OPEN);
	}

	/** One part of a closed door. */
	public BlockState part(Direction.Axis axis, int column, DoubleBlockHalf half) {
		return this.defaultBlockState().setValue(AXIS, axis).setValue(COLUMN, column).setValue(HALF, half);
	}

	// --- Fitting one ------------------------------------------------------------------------------

	/** The six blocks of a door in this frame: bottom row first, each row along the wall. */
	public static List<BlockPos> positions(Edge frame) {
		List<BlockPos> positions = new ArrayList<>(6);

		for (int h = 1; h <= 2; h++) {
			for (int u = 0; u < 3; u++) {
				positions.add(frame.pos(u, h));
			}
		}

		return positions;
	}

	/** Why a garage door cannot go on this edge (a message key), or null if it can. */
	@Nullable
	public String problem(Level level, Edge frame) {
		if (!Structure.hasWall(level, frame) || new PieceRef.Wall(frame).type(level) != PieceType.WALL_FRAME) {
			return "garage_door_needs_frame";
		}

		for (BlockPos pos : positions(frame)) {
			BlockState existing = level.getBlockState(pos);

			if (existing.is(this)) {
				return "garage_door_exists";
			}

			if (!existing.canBeReplaced()) {
				return "blocked";
			}

			if (!level.isUnobstructed(this.defaultBlockState().setValue(AXIS, frame.axis()), pos, CollisionContext.empty())) {
				return "obstructed";
			}
		}

		return null;
	}

	/** Fits a closed door into a frame; check {@link #problem} first. */
	public void install(Level level, Edge frame) {
		List<BlockPos> positions = positions(frame);

		for (int i = 0; i < positions.size(); i++) {
			BlockState state = this.part(frame.axis(), i % 3, i < 3 ? DoubleBlockHalf.LOWER : DoubleBlockHalf.UPPER);
			level.setBlock(positions.get(i), state, Block.UPDATE_ALL);
		}
	}

	// --- Where the parts are ---------------------------------------------------------------------

	private static Direction along(Direction.Axis axis) {
		return axis == Direction.Axis.X ? Direction.EAST : Direction.SOUTH;
	}

	/** The controller: the top block of the first column. */
	public static BlockPos controller(BlockPos pos, BlockState state) {
		BlockPos column = pos.relative(along(state.getValue(AXIS)), -state.getValue(COLUMN));
		return state.getValue(HALF) == DoubleBlockHalf.LOWER ? column.above() : column;
	}

	/** One row of three blocks, given the controller. */
	public static List<BlockPos> row(BlockPos controller, Direction.Axis axis, DoubleBlockHalf half) {
		BlockPos start = half == DoubleBlockHalf.UPPER ? controller : controller.below();
		List<BlockPos> row = new ArrayList<>(3);

		for (int column = 0; column < 3; column++) {
			row.add(start.relative(along(axis), column));
		}

		return row;
	}

	/** All six blocks, given the controller. */
	public static List<BlockPos> parts(BlockPos controller, Direction.Axis axis) {
		List<BlockPos> parts = new ArrayList<>(row(controller, axis, DoubleBlockHalf.LOWER));
		parts.addAll(row(controller, axis, DoubleBlockHalf.UPPER));
		return parts;
	}

	// --- Shape -----------------------------------------------------------------------------------

	@Override
	protected VoxelShape getShape(BlockState state, BlockGetter level, BlockPos pos, CollisionContext context) {
		boolean alongX = state.getValue(AXIS) == Direction.Axis.X;

		if (!state.getValue(OPEN)) {
			return alongX ? PANEL_X : PANEL_Z;
		}

		return state.getValue(HALF) == DoubleBlockHalf.UPPER ? (alongX ? ROLLED_X : ROLLED_Z) : Shapes.empty();
	}

	@Override
	protected VoxelShape getCollisionShape(BlockState state, BlockGetter level, BlockPos pos, CollisionContext context) {
		if (state.getValue(OPEN)) {
			return Shapes.empty();
		}

		return state.getValue(AXIS) == Direction.Axis.X ? PANEL_X : PANEL_Z;
	}

	// --- Lock and opening ------------------------------------------------------------------------

	@Nullable
	@Override
	public BlockEntity newBlockEntity(BlockPos pos, BlockState state) {
		return state.getValue(COLUMN) == 0 && state.getValue(HALF) == DoubleBlockHalf.UPPER ? new GarageDoorBlockEntity(pos, state) : null;
	}

	@Override
	protected InteractionResult useItemOn(ItemStack stack, BlockState state, Level level, BlockPos pos, Player player, InteractionHand hand, BlockHitResult hit) {
		// Let a code lock reach its item behaviour instead of opening the door.
		if (stack.is(ModItems.CODE_LOCK)) {
			return InteractionResult.PASS;
		}

		return super.useItemOn(stack, state, level, pos, player, hand, hit);
	}

	@Override
	protected InteractionResult useWithoutItem(BlockState state, Level level, BlockPos pos, Player player, BlockHitResult hit) {
		BlockPos controller = controller(pos, state);

		if (!(level.getBlockEntity(controller) instanceof GarageDoorBlockEntity door)) {
			return InteractionResult.PASS;
		}

		CodeLock lock = door.lock();

		if (lock.isLocked()) {
			if (player.isShiftKeyDown() && lock.isOwner(player)) {
				if (level.isClientSide()) {
					ClientBridge.get().openCodeLock(controller, LockScreenMode.OWNER);
				}

				return InteractionResult.SUCCESS;
			}

			if (!lock.canAccess(player)) {
				if (level.isClientSide()) {
					ClientBridge.get().openCodeLock(controller, LockScreenMode.ENTER);
				}

				return InteractionResult.SUCCESS;
			}
		}

		if (level instanceof ServerLevel serverLevel) {
			this.toggle(serverLevel, controller, state.getValue(AXIS), door);
		}

		return InteractionResult.SUCCESS;
	}

	/**
	 * Starts the door rolling the other way: the bottom row first when opening, the top row first when
	 * closing. It does not start closing while someone stands in the doorway.
	 */
	public void toggle(ServerLevel level, BlockPos controller, Direction.Axis axis, GarageDoorBlockEntity door) {
		boolean open = !door.isOpen();

		if (!open && this.blocked(level, controller, axis, true)) {
			level.playSound(null, controller, SoundEvents.IRON_TRAPDOOR_CLOSE, SoundSource.BLOCKS, 1.0F, 0.5F);
			return;
		}

		door.setOpen(open);
		this.setRow(level, controller, axis, open ? DoubleBlockHalf.LOWER : DoubleBlockHalf.UPPER, open);
		level.scheduleTick(controller, this, ROLL_TICKS);
		level.playSound(null, controller, open ? SoundEvents.IRON_DOOR_OPEN : SoundEvents.IRON_DOOR_CLOSE, SoundSource.BLOCKS, 1.0F, 0.6F);
	}

	/** The second half of a roll, on the controller. */
	@Override
	protected void tick(BlockState state, ServerLevel level, BlockPos pos, RandomSource random) {
		if (!(level.getBlockEntity(pos) instanceof GarageDoorBlockEntity door)) {
			return;
		}

		Direction.Axis axis = state.getValue(AXIS);

		if (door.isOpen()) {
			this.setRow(level, pos, axis, DoubleBlockHalf.UPPER, true);
		} else if (this.blocked(level, pos, axis, false)) {
			// Someone is standing in the doorway: roll back up rather than close on them.
			door.setOpen(true);
			this.setRow(level, pos, axis, DoubleBlockHalf.UPPER, true);
			level.playSound(null, pos, SoundEvents.IRON_DOOR_OPEN, SoundSource.BLOCKS, 1.0F, 0.8F);
		} else {
			this.setRow(level, pos, axis, DoubleBlockHalf.LOWER, false);
		}
	}

	private void setRow(ServerLevel level, BlockPos controller, Direction.Axis axis, DoubleBlockHalf half, boolean open) {
		for (BlockPos part : row(controller, axis, half)) {
			BlockState state = level.getBlockState(part);

			if (state.is(this) && state.getValue(OPEN) != open) {
				level.setBlock(part, state.setValue(OPEN, open), Block.UPDATE_ALL);
			}
		}
	}

	/** Whether anyone stands where the bottom row (or, with {@code bothRows}, the whole door) would close. */
	private boolean blocked(ServerLevel level, BlockPos controller, Direction.Axis axis, boolean bothRows) {
		List<BlockPos> bottom = row(controller, axis, DoubleBlockHalf.LOWER);
		BlockPos first = bottom.getFirst();
		BlockPos last = bottom.getLast();
		double top = first.getY() + (bothRows ? 2.0 : 1.0);
		AABB space = axis == Direction.Axis.X
				? new AABB(first.getX(), first.getY(), first.getZ() + 0.375, last.getX() + 1.0, top, first.getZ() + 0.625)
				: new AABB(first.getX() + 0.375, first.getY(), first.getZ(), first.getX() + 0.625, top, last.getZ() + 1.0);
		return !level.getEntitiesOfClass(LivingEntity.class, space, entity -> !entity.isSpectator()).isEmpty();
	}

	// --- Removal and raiding ---------------------------------------------------------------------

	/** Takes the whole door out, handing back the item when {@code drop} (an owner taking a wall down). */
	public void removeAll(Level level, BlockPos pos, BlockState state, boolean drop) {
		BlockPos controller = controller(pos, state);

		if (drop) {
			Block.popResource(level, controller, new ItemStack(ModItems.GARAGE_DOOR));
		}

		for (BlockPos part : parts(controller, state.getValue(AXIS))) {
			if (level.getBlockState(part).is(this)) {
				level.setBlock(part, Blocks.AIR.defaultBlockState(), Block.UPDATE_ALL);
			}
		}

		if (level instanceof ServerLevel serverLevel) {
			PieceDamage.get(serverLevel).clear(controller);
		}
	}

	/** One part gone - broken, blown up or replaced - takes the rest of the door with it. */
	@Override
	public void affectNeighborsAfterRemoval(BlockState state, ServerLevel level, BlockPos pos, boolean moved) {
		if (!level.getBlockState(pos).is(this)) {
			BlockPos controller = controller(pos, state);

			for (BlockPos part : parts(controller, state.getValue(AXIS))) {
				if (!part.equals(pos) && level.getBlockState(part).is(this)) {
					level.setBlock(part, Blocks.AIR.defaultBlockState(), Block.UPDATE_ALL);
				}
			}

			PieceDamage.get(level).clear(controller);
		}

		super.affectNeighborsAfterRemoval(state, level, pos, moved);
	}

	@Override
	protected void onExplosionHit(BlockState state, ServerLevel level, BlockPos pos, Explosion explosion, BiConsumer<ItemStack, BlockPos> dropConsumer) {
		BlockPos controller = controller(pos, state);

		if (RaidDamage.damageDoor(level, controller, parts(controller, state.getValue(AXIS)), MAX_HEALTH, explosion)) {
			this.removeAll(level, pos, state, false);
		}
	}
}
