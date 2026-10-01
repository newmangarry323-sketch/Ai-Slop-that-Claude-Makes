package io.github.newmangarry323sketch.rustbuilding.block;

import java.util.function.BiConsumer;

import net.minecraft.core.BlockPos;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.level.Explosion;
import net.minecraft.world.level.block.StairBlock;
import net.minecraft.world.level.block.state.BlockState;

import io.github.newmangarry323sketch.rustbuilding.building.BuildingTier;
import io.github.newmangarry323sketch.rustbuilding.raid.RaidDamage;

/** A step of a staircase piece, in one grade. */
public class BuildingStairsBlock extends StairBlock {
	private final BuildingTier tier;

	public BuildingStairsBlock(BuildingTier tier, BlockState baseState, Properties properties) {
		super(baseState, properties);
		this.tier = tier;
	}

	public BuildingTier tier() {
		return this.tier;
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
