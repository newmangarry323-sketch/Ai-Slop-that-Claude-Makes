package io.github.newmangarry323sketch.rustbuilding.block;

import java.util.function.BiConsumer;

import net.minecraft.core.BlockPos;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.level.Explosion;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.state.StateDefinition;
import net.minecraft.world.level.block.state.properties.EnumProperty;

import io.github.newmangarry323sketch.rustbuilding.building.BuildingKind;
import io.github.newmangarry323sketch.rustbuilding.building.BuildingTier;
import io.github.newmangarry323sketch.rustbuilding.raid.RaidDamage;

/**
 * One block of a building piece. There is one of these per grade. Wood and above cannot be mined -
 * like Rust, a base is taken apart with its owner's hammer or with explosives - while twig breaks by
 * hand.
 */
public class BuildingBlock extends Block {
	public static final EnumProperty<BuildingKind> KIND = EnumProperty.create("kind", BuildingKind.class);

	private final BuildingTier tier;

	public BuildingBlock(BuildingTier tier, Properties properties) {
		super(properties);
		this.tier = tier;
		this.registerDefaultState(this.stateDefinition.any().setValue(KIND, BuildingKind.WALL));
	}

	public BuildingTier tier() {
		return this.tier;
	}

	@Override
	protected void createBlockStateDefinition(StateDefinition.Builder<Block, BlockState> builder) {
		builder.add(KIND);
	}

	@Override
	protected void onExplosionHit(BlockState state, ServerLevel level, BlockPos pos, Explosion explosion, BiConsumer<ItemStack, BlockPos> dropConsumer) {
		if (this.tier == BuildingTier.TWIG) {
			super.onExplosionHit(state, level, pos, explosion, dropConsumer);
		} else {
			RaidDamage.onExplosionHit(level, pos, state, explosion);
		}
	}
}
