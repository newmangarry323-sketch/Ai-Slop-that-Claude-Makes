package io.github.newmangarry323sketch.rustbuilding.client.screen;

import net.minecraft.client.Minecraft;
import net.minecraft.core.BlockPos;
import net.minecraft.network.chat.Component;
import net.minecraft.world.level.block.state.BlockState;

import net.fabricmc.fabric.api.client.networking.v1.ClientPlayNetworking;

import io.github.newmangarry323sketch.rustbuilding.building.BuildingTier;
import io.github.newmangarry323sketch.rustbuilding.building.Costs;
import io.github.newmangarry323sketch.rustbuilding.building.PieceLocator;
import io.github.newmangarry323sketch.rustbuilding.building.PieceRef;
import io.github.newmangarry323sketch.rustbuilding.network.Payloads;

/** The hammer menu: upgrade straight to any higher grade, repair, or demolish. */
public class HammerMenuScreen extends MenuScreen {
	private final BlockPos pos;

	public HammerMenuScreen(BlockPos pos) {
		super(Component.translatable("screen.rustbuilding.hammer"));
		this.pos = pos;
	}

	@Override
	protected void build() {
		Minecraft minecraft = Minecraft.getInstance();

		if (minecraft.level == null || minecraft.player == null) {
			return;
		}

		BlockState state = minecraft.level.getBlockState(this.pos);
		PieceRef ref = PieceLocator.locate(minecraft.level, this.pos, state, minecraft.player.getEyePosition());
		BuildingTier tier = ref == null ? null : ref.tier(minecraft.level);

		if (ref == null || tier == null) {
			this.lines().add(Component.translatable("screen.rustbuilding.hammer.gone"));
			this.button(Component.translatable("gui.done"), button -> this.onClose());
			return;
		}

		int units = ref.units(minecraft.level);
		this.lines().add(ref.describe(minecraft.level));
		this.lines().add(Component.translatable("screen.rustbuilding.hammer.health", tier.health()));

		for (BuildingTier target : BuildingTier.values()) {
			if (target.ordinal() <= tier.ordinal()) {
				continue;
			}

			int cost = target.cost(units);
			Component label = Component.translatable("screen.rustbuilding.hammer.upgrade", target.displayName(), Costs.describe(target, cost));
			this.button(label, button -> this.send(Payloads.HammerAction.UPGRADE, target.ordinal()));
		}

		this.button(Component.translatable("screen.rustbuilding.hammer.repair"), button -> this.send(Payloads.HammerAction.REPAIR, 0));
		this.button(Component.translatable("screen.rustbuilding.hammer.demolish", Costs.describe(tier, tier.cost(units) / 2)),
				button -> this.send(Payloads.HammerAction.DEMOLISH, 0));
		this.button(Component.translatable("gui.cancel"), button -> this.onClose());
	}

	private void send(int action, int tier) {
		ClientPlayNetworking.send(new Payloads.HammerAction(this.pos, action, tier));
		this.onClose();
	}
}
