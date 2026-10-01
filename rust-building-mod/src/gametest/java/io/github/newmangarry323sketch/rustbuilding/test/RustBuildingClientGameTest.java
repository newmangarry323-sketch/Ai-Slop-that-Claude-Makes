package io.github.newmangarry323sketch.rustbuilding.test;

import net.minecraft.client.gui.screens.inventory.DispenserScreen;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.entity.player.ChatVisiblity;
import net.minecraft.world.item.Item;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.levelgen.Heightmap;

import net.fabricmc.fabric.api.client.gametest.v1.FabricClientGameTest;
import net.fabricmc.fabric.api.client.gametest.v1.context.ClientGameTestContext;
import net.fabricmc.fabric.api.client.gametest.v1.context.TestSingleplayerContext;
import net.fabricmc.fabric.api.client.networking.v1.ClientPlayNetworking;

import io.github.newmangarry323sketch.rustbuilding.LockScreenMode;
import io.github.newmangarry323sketch.rustbuilding.block.GarageDoorBlock;
import io.github.newmangarry323sketch.rustbuilding.block.ToolCupboardBlock;
import io.github.newmangarry323sketch.rustbuilding.block.entity.ToolCupboardBlockEntity;
import io.github.newmangarry323sketch.rustbuilding.building.BuildingTier;
import io.github.newmangarry323sketch.rustbuilding.building.Edge;
import io.github.newmangarry323sketch.rustbuilding.building.Grid;
import io.github.newmangarry323sketch.rustbuilding.building.PieceRef;
import io.github.newmangarry323sketch.rustbuilding.building.PieceType;
import io.github.newmangarry323sketch.rustbuilding.building.Structure;
import io.github.newmangarry323sketch.rustbuilding.client.screen.CodeLockScreen;
import io.github.newmangarry323sketch.rustbuilding.client.screen.PieceMenuScreen;
import io.github.newmangarry323sketch.rustbuilding.client.screen.ToolCupboardScreen;
import io.github.newmangarry323sketch.rustbuilding.network.Payloads;
import io.github.newmangarry323sketch.rustbuilding.registry.ModBlocks;
import io.github.newmangarry323sketch.rustbuilding.registry.ModComponents;
import io.github.newmangarry323sketch.rustbuilding.registry.ModItems;

/**
 * Starts the real client, builds a small base and photographs it, opens its garage door and shows its
 * tool cupboard's upkeep, then uses a building plan and a hammer with real right clicks - placing a
 * wall where the preview showed it, then upgrading it - and photographs two of the menus. It exercises
 * the client-only code (preview rendering, HUD, screens) and the client-to-server path that server
 * tests cannot reach.
 */
@SuppressWarnings("UnstableApiUsage")
public class RustBuildingClientGameTest implements FabricClientGameTest {
	/**
	 * The "new recipes" toast stays up for five seconds of real time, and the game never runs more than
	 * 20 ticks a second, so this many ticks is always long enough for it to go.
	 */
	private static final int TOAST_TICKS = 140;
	/** The name of a newly held item shows over the hotbar for two seconds. */
	private static final int ITEM_NAME_TICKS = 60;

	@Override
	public void runTest(ClientGameTestContext context) {
		try (TestSingleplayerContext singleplayer = context.worldBuilder().create()) {
			singleplayer.getConnection().waitForChunksRender();

			// The commands below report back in the chat, which would end up in every screenshot.
			context.runOnClient(client -> client.options.chatVisibility().set(ChatVisiblity.HIDDEN));

			// First free block above the flat test world's surface; foundations sit there.
			int ground = singleplayer.getServer().computeOnServer(server ->
					server.overworld().getHeight(Heightmap.Types.MOTION_BLOCKING_NO_LEAVES, 8, 8));

			// Out of the way first: the planner refuses to build where someone is standing.
			singleplayer.getServer().runCommand("/tp @a -16.5 " + ground + " -16.5");
			BlockPos cupboardPos = Grid.slabAnchor(2, 1, ground).above();
			singleplayer.getServer().runOnServer(server -> {
				ServerLevel level = server.overworld();
				TestBuilds.demoBase(level, 1, 1, ground);

				// A tool cupboard inside, facing the garage door, stocked for upkeep.
				level.setBlock(cupboardPos, ModBlocks.TOOL_CUPBOARD.defaultBlockState().setValue(ToolCupboardBlock.FACING, Direction.EAST), Block.UPDATE_ALL);
				ToolCupboardBlockEntity cupboard = (ToolCupboardBlockEntity) level.getBlockEntity(cupboardPos);
				cupboard.authorize(player(server));
				Item[] upkeep = {Items.STICK, Items.OAK_PLANKS, Items.COBBLESTONE, Items.IRON_NUGGET, Items.IRON_INGOT};

				for (int slot = 0; slot < upkeep.length; slot++) {
					cupboard.storage().setItem(slot, new ItemStack(upkeep[slot], 64));
				}

				cupboard.settle(level, level.getGameTime());

				// Materials for later, handed out now: the first sticks and planks unlock recipes, and the
				// toast that announces them has to be gone before the first screenshot.
				player(server).getInventory().setItem(1, new ItemStack(Items.STICK, 32));
				player(server).getInventory().setItem(2, new ItemStack(Items.OAK_PLANKS, 16));
			});

			singleplayer.getServer().runCommand("/time set noon");
			singleplayer.getServer().runCommand("/weather clear");

			// The base from above its south-east corner. A spectator does not fall, and has no hand or
			// hotbar in the way.
			singleplayer.getServer().runCommand("/gamemode spectator @a");
			singleplayer.getServer().runCommand("/tp @a 17.5 " + (ground + 5) + " 17.5 135 19");
			singleplayer.getConnection().waitForChunksRender();
			context.waitTicks(TOAST_TICKS);
			context.takeScreenshot("rustbuilding-base");

			// The garage door in the base's east wall, from outside; then open it with a real right click.
			singleplayer.getServer().runCommand("/gamemode survival @a");
			singleplayer.getServer().runCommand("/tp @a 16.5 " + ground + " 6.5 90 0");
			singleplayer.getConnection().waitForChunksRender();
			context.waitTicks(20);
			context.takeScreenshot("rustbuilding-garage-door");

			Edge garage = Edge.alongZ(Grid.lineOf(3), 1, ground);
			context.getInput().pressKey(options -> options.keyUse);
			singleplayer.getServer().waitFor(server -> {
				for (BlockPos part : GarageDoorBlock.positions(garage)) {
					BlockState state = server.overworld().getBlockState(part);

					if (!state.is(ModBlocks.GARAGE_DOOR) || !state.getValue(GarageDoorBlock.OPEN)) {
						return false;
					}
				}

				return true;
			}, 100);
			context.waitTicks(5);
			context.takeScreenshot("rustbuilding-garage-door-open");

			// The cupboard's screen with its upkeep, and its storage, opened the way its button does.
			context.setScreen(() -> new ToolCupboardScreen(cupboardPos));
			context.waitTicks(2);
			context.takeScreenshot("rustbuilding-cupboard");
			context.setScreen(() -> null);
			context.runOnClient(client -> ClientPlayNetworking.send(new Payloads.CupboardAction(cupboardPos, Payloads.CupboardAction.OPEN_STORAGE)));
			context.waitForScreen(DispenserScreen.class);
			context.waitTicks(2);
			context.takeScreenshot("rustbuilding-cupboard-storage");
			context.setScreen(() -> null);

			// A spare foundation, and a building plan set to Wall, aimed at the foundation's far edge. The
			// player moves first: they were standing where the foundation goes.
			singleplayer.getServer().runCommand("/tp @a 18.5 " + ground + " 11.5 180 6");
			singleplayer.getServer().runOnServer(server -> {
				TestBuilds.slab(server.overworld(), PieceType.FOUNDATION, 4, 1, ground);
				ItemStack plan = new ItemStack(ModItems.BUILDING_PLAN);
				plan.set(ModComponents.SELECTED_PIECE, PieceType.WALL.ordinal());
				player(server).setItemInHand(InteractionHand.MAIN_HAND, plan);
			});
			singleplayer.getConnection().waitForChunksRender();
			context.waitTicks(ITEM_NAME_TICKS);
			context.takeScreenshot("rustbuilding-preview");

			// Now for real: a right click goes through the client, over the network and into the item on
			// the server, which places the twig wall the preview showed and takes 9 sticks.
			Edge previewed = Edge.alongX(Grid.lineOf(1), 4, ground);
			context.getInput().pressKey(options -> options.keyUse);
			singleplayer.getServer().waitFor(server -> Structure.hasWall(server.overworld(), previewed), 100);

			// Put the plan away, or its preview turns red over the new wall: there is a wall there now.
			singleplayer.getServer().runOnServer(server -> player(server).setItemInHand(InteractionHand.MAIN_HAND, ItemStack.EMPTY));
			context.waitTicks(5);
			context.takeScreenshot("rustbuilding-placed");

			// Walk up to the new wall with a hammer, and upgrade it to wood with the planks.
			singleplayer.getServer().runOnServer(server -> player(server).setItemInHand(InteractionHand.MAIN_HAND, new ItemStack(ModItems.HAMMER)));
			singleplayer.getServer().runCommand("/tp @a 18.5 " + (ground + 1) + " 8.8 180 8");
			singleplayer.getConnection().waitForChunksRender();
			context.waitTicks(ITEM_NAME_TICKS);
			context.takeScreenshot("rustbuilding-hammer");

			context.getInput().pressKey(options -> options.keyUse);
			singleplayer.getServer().waitFor(server -> new PieceRef.Wall(previewed).tier(server.overworld()) == BuildingTier.WOOD, 100);
			context.waitTicks(5);
			context.takeScreenshot("rustbuilding-upgraded");

			context.setScreen(() -> new PieceMenuScreen(PieceType.WALL));
			// The mouse sits in the middle of the screen; move it off the buttons so none looks hovered.
			context.getInput().setCursorPos(0, 0);
			context.waitTicks(2);
			context.takeScreenshot("rustbuilding-piece-menu");

			context.setScreen(() -> new CodeLockScreen(new BlockPos(18, ground, 6), LockScreenMode.ENTER));
			context.waitTicks(2);
			context.takeScreenshot("rustbuilding-code-lock");
			context.setScreen(() -> null);
		}
	}

	private static ServerPlayer player(MinecraftServer server) {
		return server.getPlayerList().getPlayers().getFirst();
	}
}
