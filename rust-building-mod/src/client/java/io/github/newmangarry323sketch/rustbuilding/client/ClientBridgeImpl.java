package io.github.newmangarry323sketch.rustbuilding.client;

import net.minecraft.client.Minecraft;
import net.minecraft.core.BlockPos;
import net.minecraft.world.item.ItemStack;

import io.github.newmangarry323sketch.rustbuilding.ClientBridge;
import io.github.newmangarry323sketch.rustbuilding.LockScreenMode;
import io.github.newmangarry323sketch.rustbuilding.block.entity.ToolCupboardBlockEntity;
import io.github.newmangarry323sketch.rustbuilding.building.PieceType;
import io.github.newmangarry323sketch.rustbuilding.client.screen.CodeLockScreen;
import io.github.newmangarry323sketch.rustbuilding.client.screen.HammerMenuScreen;
import io.github.newmangarry323sketch.rustbuilding.client.screen.PieceMenuScreen;
import io.github.newmangarry323sketch.rustbuilding.client.screen.ToolCupboardScreen;
import io.github.newmangarry323sketch.rustbuilding.item.BuildingPlanItem;
import io.github.newmangarry323sketch.rustbuilding.registry.ModItems;

final class ClientBridgeImpl implements ClientBridge {
	@Override
	public void openPieceMenu() {
		Minecraft minecraft = Minecraft.getInstance();

		if (minecraft.player == null) {
			return;
		}

		ItemStack main = minecraft.player.getMainHandItem();
		ItemStack plan = main.is(ModItems.BUILDING_PLAN) ? main : minecraft.player.getOffhandItem();
		PieceType current = plan.is(ModItems.BUILDING_PLAN) ? BuildingPlanItem.selected(plan) : PieceType.FOUNDATION;
		minecraft.gui.setScreen(new PieceMenuScreen(current));
	}

	@Override
	public void openHammerMenu(BlockPos pos) {
		Minecraft.getInstance().gui.setScreen(new HammerMenuScreen(pos));
	}

	@Override
	public void openToolCupboard(BlockPos pos) {
		Minecraft minecraft = Minecraft.getInstance();

		if (minecraft.level == null || minecraft.player == null
				|| !(minecraft.level.getBlockEntity(pos) instanceof ToolCupboardBlockEntity cupboard)) {
			return;
		}

		if (!cupboard.lock().canAccess(minecraft.player)) {
			minecraft.gui.setScreen(new CodeLockScreen(pos, LockScreenMode.ENTER));
		} else {
			minecraft.gui.setScreen(new ToolCupboardScreen(pos));
		}
	}

	@Override
	public void openCodeLock(BlockPos pos, LockScreenMode mode) {
		Minecraft.getInstance().gui.setScreen(new CodeLockScreen(pos, mode));
	}
}
