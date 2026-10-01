package io.github.newmangarry323sketch.rustbuilding.item;

import java.util.function.Consumer;

import org.jspecify.annotations.Nullable;

import net.minecraft.ChatFormatting;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.network.chat.Component;
import net.minecraft.sounds.SoundEvents;
import net.minecraft.sounds.SoundSource;
import net.minecraft.world.InteractionResult;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.item.BlockItem;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.TooltipFlag;
import net.minecraft.world.item.component.TooltipDisplay;
import net.minecraft.world.item.context.UseOnContext;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.block.state.BlockState;

import io.github.newmangarry323sketch.rustbuilding.block.GarageDoorBlock;
import io.github.newmangarry323sketch.rustbuilding.building.BuildingKind;
import io.github.newmangarry323sketch.rustbuilding.building.Edge;
import io.github.newmangarry323sketch.rustbuilding.building.PieceLocator;
import io.github.newmangarry323sketch.rustbuilding.building.PiecePlanner;
import io.github.newmangarry323sketch.rustbuilding.building.PieceRef;
import io.github.newmangarry323sketch.rustbuilding.building.Structure;
import io.github.newmangarry323sketch.rustbuilding.privilege.BuildingPrivilege;

/**
 * A garage door. Use it on a wall frame - the frame itself, or the floor in its opening - and it fills
 * the opening, closed. The rules are the block's ({@link GarageDoorBlock#problem}).
 */
public class GarageDoorItem extends BlockItem {
	public GarageDoorItem(Block block, Properties properties) {
		super(block, properties);
	}

	@Override
	public InteractionResult useOn(UseOnContext context) {
		Level level = context.getLevel();
		Player player = context.getPlayer();

		if (player == null) {
			return InteractionResult.PASS;
		}

		Edge frame = frameAt(level, context.getClickedPos(), context.getClickedFace(), context, player);
		GarageDoorBlock block = (GarageDoorBlock) this.getBlock();
		String problem = frame == null ? "garage_door_needs_frame" : block.problem(level, frame);

		if (problem != null) {
			return fail(level, player, problem);
		}

		if (!BuildingPrivilege.canBuildAll(level, player, GarageDoorBlock.positions(frame))) {
			return fail(level, player, "building_blocked");
		}

		if (!level.isClientSide()) {
			block.install(level, frame);
			level.playSound(null, frame.pos(1, 2), SoundEvents.IRON_DOOR_CLOSE, SoundSource.BLOCKS, 1.0F, 0.7F);

			if (!player.hasInfiniteMaterials()) {
				context.getItemInHand().shrink(1);
			}
		}

		return InteractionResult.SUCCESS;
	}

	/** The wall frame the player means: the frame they clicked, or the one beside the floor they clicked. */
	@Nullable
	private static Edge frameAt(Level level, BlockPos clicked, Direction face, UseOnContext context, Player player) {
		BlockState state = level.getBlockState(clicked);
		Edge edge = null;

		if (Structure.kindOf(state) == BuildingKind.WALL) {
			PieceRef ref = PieceLocator.locate(level, clicked, state, player.getEyePosition());
			edge = ref instanceof PieceRef.Wall wall ? wall.edge() : null;
		} else if (Structure.kindOf(state) == BuildingKind.SLAB && face == Direction.UP) {
			edge = PiecePlanner.nearestEdge(clicked, context.getClickLocation());
		}

		return edge;
	}

	private static InteractionResult fail(Level level, Player player, String reason) {
		if (!level.isClientSide()) {
			player.sendOverlayMessage(Component.translatable("message.rustbuilding." + reason).withStyle(ChatFormatting.RED));
		}

		return InteractionResult.FAIL;
	}

	@Override
	public void appendHoverText(ItemStack stack, TooltipContext context, TooltipDisplay display, Consumer<Component> tooltip, TooltipFlag flag) {
		tooltip.accept(Component.translatable("block.rustbuilding.garage_door.hint").withStyle(ChatFormatting.GRAY));
	}
}
