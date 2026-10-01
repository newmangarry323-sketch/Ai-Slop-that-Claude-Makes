package io.github.newmangarry323sketch.rustbuilding.test;

import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.level.levelgen.Heightmap;

import net.fabricmc.fabric.api.client.gametest.v1.FabricClientGameTest;
import net.fabricmc.fabric.api.client.gametest.v1.context.ClientGameTestContext;
import net.fabricmc.fabric.api.client.gametest.v1.context.TestSingleplayerContext;

import io.github.newmangarry323sketch.rustbuilding.LockScreenMode;
import io.github.newmangarry323sketch.rustbuilding.building.PieceType;
import io.github.newmangarry323sketch.rustbuilding.client.screen.CodeLockScreen;
import io.github.newmangarry323sketch.rustbuilding.client.screen.PieceMenuScreen;
import io.github.newmangarry323sketch.rustbuilding.registry.ModComponents;
import io.github.newmangarry323sketch.rustbuilding.registry.ModItems;

/**
 * Starts the real client, builds a small base and photographs it, the placement preview and two of the
 * menus. It exercises the client-only code (preview rendering, HUD, screens) that server tests cannot
 * reach; the screenshots land in build/run/clientGameTest/screenshots.
 */
@SuppressWarnings("UnstableApiUsage")
public class RustBuildingClientGameTest implements FabricClientGameTest {
	@Override
	public void runTest(ClientGameTestContext context) {
		try (TestSingleplayerContext singleplayer = context.worldBuilder().create()) {
			singleplayer.getConnection().waitForChunksRender();

			int ground = singleplayer.getServer().computeOnServer(server -> {
				ServerLevel level = server.overworld();
				// First free block above the flat test world's surface; foundations sit there.
				int y = level.getHeight(Heightmap.Types.MOTION_BLOCKING_NO_LEAVES, 8, 8);
				TestBuilds.demoBase(level, 1, 1, y);
				TestBuilds.slab(level, PieceType.FOUNDATION, 4, 1, y);
				return y;
			});

			singleplayer.getServer().runCommand("/time set noon");
			singleplayer.getServer().runCommand("/weather clear");

			// The base, from the south-east.
			singleplayer.getServer().runCommand("/tp @a 17.5 " + (ground + 6) + " 19.5 138 17");
			singleplayer.getConnection().waitForChunksRender();
			context.waitTicks(20);
			context.takeScreenshot("rustbuilding-base");

			// Holding a building plan set to Wall, aimed at the far edge of the spare foundation.
			singleplayer.getServer().runOnServer(server -> {
				ServerPlayer player = server.getPlayerList().getPlayers().getFirst();
				ItemStack plan = new ItemStack(ModItems.BUILDING_PLAN);
				plan.set(ModComponents.SELECTED_PIECE, PieceType.WALL.ordinal());
				player.setItemInHand(InteractionHand.MAIN_HAND, plan);
			});
			singleplayer.getServer().runCommand("/tp @a 18.5 " + ground + " 11.5 180 6");
			singleplayer.getConnection().waitForChunksRender();
			context.waitTicks(10);
			context.takeScreenshot("rustbuilding-preview");

			context.setScreen(() -> new PieceMenuScreen(PieceType.WALL));
			context.waitTicks(2);
			context.takeScreenshot("rustbuilding-piece-menu");

			context.setScreen(() -> new CodeLockScreen(new net.minecraft.core.BlockPos(18, ground, 6), LockScreenMode.ENTER));
			context.waitTicks(2);
			context.takeScreenshot("rustbuilding-code-lock");
			context.setScreen(() -> null);
		}
	}
}
