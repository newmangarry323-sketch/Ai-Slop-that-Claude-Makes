package io.github.newmangarry323sketch.rustbuilding.test;

import java.util.List;

import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.gametest.framework.GameTestHelper;
import net.minecraft.network.chat.Component;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.SimpleContainer;
import net.minecraft.world.entity.decoration.ArmorStand;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.DoorBlock;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.state.properties.DoubleBlockHalf;
import net.minecraft.world.phys.Vec3;

import net.fabricmc.fabric.api.gametest.v1.GameTest;

import io.github.newmangarry323sketch.rustbuilding.block.GarageDoorBlock;
import io.github.newmangarry323sketch.rustbuilding.block.entity.GarageDoorBlockEntity;
import io.github.newmangarry323sketch.rustbuilding.block.entity.ToolCupboardBlockEntity;
import io.github.newmangarry323sketch.rustbuilding.building.BuildingKind;
import io.github.newmangarry323sketch.rustbuilding.building.BuildingOps;
import io.github.newmangarry323sketch.rustbuilding.building.BuildingTier;
import io.github.newmangarry323sketch.rustbuilding.building.Edge;
import io.github.newmangarry323sketch.rustbuilding.building.Grid;
import io.github.newmangarry323sketch.rustbuilding.building.PieceLocator;
import io.github.newmangarry323sketch.rustbuilding.building.PieceRef;
import io.github.newmangarry323sketch.rustbuilding.building.PieceType;
import io.github.newmangarry323sketch.rustbuilding.building.PlannedPiece;
import io.github.newmangarry323sketch.rustbuilding.building.Structure;
import io.github.newmangarry323sketch.rustbuilding.lock.LockHolder;
import io.github.newmangarry323sketch.rustbuilding.raid.PieceDamage;
import io.github.newmangarry323sketch.rustbuilding.raid.RaidDamage;
import io.github.newmangarry323sketch.rustbuilding.registry.ModBlocks;
import io.github.newmangarry323sketch.rustbuilding.upkeep.Decay;
import io.github.newmangarry323sketch.rustbuilding.upkeep.DecayClocks;
import io.github.newmangarry323sketch.rustbuilding.upkeep.Upkeep;
import io.github.newmangarry323sketch.rustbuilding.upkeep.UpkeepAccount;

/**
 * Wall frames and garage doors, upkeep and decay. Tests that put down a tool cupboard, or that need to
 * know no cupboard is near, use a 44 x 44 platform so that a cupboard's 16-block zone stays inside it.
 */
public class GarageDoorAndUpkeepGameTest {
	private static final String PLATFORM = "rustbuilding-test:platform";
	private static final String WIDE = "rustbuilding-test:wide_platform";

	private record Cell(int x, int z, int y) {
		/** The grid cell around a position on the platform (relative coordinates), at slab level 1. */
		static Cell at(GameTestHelper helper, int relativeX, int relativeZ) {
			BlockPos pos = helper.absolutePos(new BlockPos(relativeX, 1, relativeZ));
			return new Cell(Grid.cell(pos.getX()), Grid.cell(pos.getZ()), pos.getY());
		}
	}

	private static void check(GameTestHelper helper, boolean condition, String message) {
		helper.assertTrue(condition, Component.literal(message));
	}

	private static void checkClose(GameTestHelper helper, double actual, double expected, String message) {
		check(helper, Math.abs(actual - expected) < 1.0E-9, message + ": expected " + expected + ", got " + actual);
	}

	private static Vec3 centreOf(BlockPos pos) {
		return Vec3.atCenterOf(pos);
	}

	private static boolean isDoorPart(ServerLevel level, BlockPos pos, boolean open) {
		BlockState state = level.getBlockState(pos);
		return state.is(ModBlocks.GARAGE_DOOR) && state.getValue(GarageDoorBlock.OPEN) == open;
	}

	private static void checkRow(GameTestHelper helper, Edge frame, int h, boolean open, String message) {
		for (int u = 0; u < 3; u++) {
			check(helper, isDoorPart(helper.getLevel(), frame.pos(u, h), open), message + " (column " + u + ", row " + h + ": "
					+ helper.getLevel().getBlockState(frame.pos(u, h)) + ")");
		}
	}

	/** A foundation with a wall frame and a closed garage door on its north edge. */
	private static Edge frameWithDoor(ServerLevel level, Cell c) {
		TestBuilds.slab(level, PieceType.FOUNDATION, c.x(), c.z(), c.y());
		Edge north = TestBuilds.north(c.x(), c.z(), c.y());
		TestBuilds.wall(level, PieceType.WALL_FRAME, north);
		ModBlocks.GARAGE_DOOR.install(level, north);
		return north;
	}

	// --- Wall frames -----------------------------------------------------------------------------

	@GameTest(structure = PLATFORM)
	public void wallFramesStandOnFullHeightPosts(GameTestHelper helper) {
		ServerLevel level = helper.getLevel();
		Cell c = Cell.at(helper, 7, 7);
		TestBuilds.slab(level, PieceType.FOUNDATION, c.x(), c.z(), c.y());
		Edge north = TestBuilds.north(c.x(), c.z(), c.y());
		PlannedPiece frame = TestBuilds.wall(level, PieceType.WALL_FRAME, north);
		check(helper, frame.units() == 3, "a wall frame costs its 3-block beam, got " + frame.units());
		check(helper, new PieceRef.Wall(north).type(level) == PieceType.WALL_FRAME, "the frame is recognised as one");

		for (int u = 0; u < 3; u++) {
			check(helper, Structure.isKind(level, north.pos(u, 3), BuildingKind.WALL), "the beam is there at column " + u);
			check(helper, level.getBlockState(north.pos(u, 1)).isAir() && level.getBlockState(north.pos(u, 2)).isAir(), "the opening is clear at column " + u);
		}

		// Upgrading re-derives the corner posts: they must stay, full height, at the frame's grade.
		BuildingOps.upgrade(level, new PieceRef.Wall(north), BuildingTier.STONE);

		for (int end = 0; end <= 1; end++) {
			for (int h = 1; h <= 3; h++) {
				BlockState post = level.getBlockState(north.corner(end, h));
				check(helper, Structure.kindOf(post) == BuildingKind.WALL && Structure.tierOf(post) == BuildingTier.STONE,
						"stone post at end " + end + ", height " + h + ", got " + post);
			}
		}

		helper.succeed();
	}

	@GameTest(structure = PLATFORM)
	public void blocksAboveOpeningsBelongToTheirWall(GameTestHelper helper) {
		ServerLevel level = helper.getLevel();
		Cell c = Cell.at(helper, 7, 7);
		TestBuilds.slab(level, PieceType.FOUNDATION, c.x(), c.z(), c.y());
		Edge north = TestBuilds.north(c.x(), c.z(), c.y());
		Edge south = TestBuilds.south(c.x(), c.z(), c.y());
		Edge east = TestBuilds.east(c.x(), c.z(), c.y());
		TestBuilds.wall(level, PieceType.WINDOW, north);
		TestBuilds.wall(level, PieceType.DOORWAY, south);
		TestBuilds.wall(level, PieceType.WALL_FRAME, east);

		// The block over a window, a doorway or a frame's opening has a gap under it.
		for (Edge edge : List.of(north, south, east)) {
			BlockPos top = edge.pos(1, 3);
			BlockState state = level.getBlockState(top);
			check(helper, Structure.storeyOf(level, top, state) == c.y(), "the storey of " + top + " is found across the opening");
			PieceRef located = PieceLocator.locate(level, top, state, centreOf(top));
			check(helper, new PieceRef.Wall(edge).equals(located), "the block over the opening of " + edge + " belongs to it, got " + located);
		}

		helper.succeed();
	}

	// --- Garage doors ----------------------------------------------------------------------------

	@GameTest(structure = PLATFORM, maxTicks = 60)
	public void garageDoorsFillAFrameAndRollUp(GameTestHelper helper) {
		ServerLevel level = helper.getLevel();
		Cell c = Cell.at(helper, 7, 7);
		GarageDoorBlock door = ModBlocks.GARAGE_DOOR;
		TestBuilds.slab(level, PieceType.FOUNDATION, c.x(), c.z(), c.y());
		Edge north = TestBuilds.north(c.x(), c.z(), c.y());
		check(helper, door.problem(level, north) != null, "no frame, no garage door");

		TestBuilds.wall(level, PieceType.WALL_FRAME, north);
		check(helper, door.problem(level, north) == null, "a frame takes a garage door, got " + door.problem(level, north));
		door.install(level, north);
		check(helper, "garage_door_exists".equals(door.problem(level, north)), "one door per frame");
		checkRow(helper, north, 1, false, "the bottom row is in, closed");
		checkRow(helper, north, 2, false, "the top row is in, closed");

		BlockPos controller = north.pos(0, 2);
		check(helper, level.getBlockEntity(controller) instanceof GarageDoorBlockEntity, "the top of the first column holds the lock");
		LockHolder fromCorner = LockHolder.find(level, north.pos(2, 1));
		check(helper, fromCorner != null && fromCorner.getBlockPos().equals(controller), "every part finds the lock");
		check(helper, new PieceRef.Wall(north).type(level) == PieceType.WALL_FRAME, "a frame with a door in it is still a frame");

		GarageDoorBlockEntity entity = (GarageDoorBlockEntity) level.getBlockEntity(controller);
		door.toggle(level, controller, Direction.Axis.X, entity);
		checkRow(helper, north, 1, true, "opening lifts the bottom row first");
		checkRow(helper, north, 2, false, "the top row follows");

		helper.runAfterDelay(GarageDoorBlock.ROLL_TICKS + 2, () -> {
			checkRow(helper, north, 2, true, "then the top row rolls up");
			door.toggle(level, controller, Direction.Axis.X, entity);
			checkRow(helper, north, 2, false, "closing lowers the top row first");
			checkRow(helper, north, 1, true, "the bottom row follows");

			helper.runAfterDelay(GarageDoorBlock.ROLL_TICKS + 2, () -> {
				checkRow(helper, north, 1, false, "and then the bottom row: closed");
				helper.succeed();
			});
		});
	}

	@GameTest(structure = PLATFORM, maxTicks = 60)
	public void garageDoorsDoNotCloseOnSomeone(GameTestHelper helper) {
		ServerLevel level = helper.getLevel();
		GarageDoorBlock door = ModBlocks.GARAGE_DOOR;
		Edge north = frameWithDoor(level, Cell.at(helper, 7, 7));
		BlockPos controller = north.pos(0, 2);
		GarageDoorBlockEntity entity = (GarageDoorBlockEntity) level.getBlockEntity(controller);
		door.toggle(level, controller, Direction.Axis.X, entity);

		helper.runAfterDelay(GarageDoorBlock.ROLL_TICKS + 2, () -> {
			checkRow(helper, north, 2, true, "open");
			BlockPos doorway = north.pos(1, 1);
			ArmorStand stand = new ArmorStand(level, doorway.getX() + 0.5, doorway.getY(), doorway.getZ() + 0.5);
			level.addFreshEntity(stand);
			door.toggle(level, controller, Direction.Axis.X, entity);
			check(helper, entity.isOpen(), "the door stays open for someone standing in it");
			checkRow(helper, north, 1, true, "bottom row still open");
			checkRow(helper, north, 2, true, "top row still open");
			stand.discard();
			helper.succeed();
		});
	}

	@GameTest(structure = PLATFORM, maxTicks = 40)
	public void garageDoorsTakeThreeTnt(GameTestHelper helper) {
		ServerLevel level = helper.getLevel();
		Cell c = Cell.at(helper, 7, 7);
		Edge north = frameWithDoor(level, c);
		// An armored foundation and frame, so only the door is at risk.
		BuildingOps.upgrade(level, new PieceRef.Slab(c.x(), c.z(), c.y()), BuildingTier.ARMORED);
		BuildingOps.upgrade(level, new PieceRef.Wall(north), BuildingTier.ARMORED);
		BlockPos controller = north.pos(0, 2);
		Vec3 blast = centreOf(north.pos(1, 1).north());

		level.explode(null, blast.x, blast.y, blast.z, 4.0F, Level.ExplosionInteraction.TNT);
		level.explode(null, blast.x, blast.y, blast.z, 4.0F, Level.ExplosionInteraction.TNT);
		check(helper, level.getBlockState(north.pos(1, 1)).is(ModBlocks.GARAGE_DOOR), "two TNT do not break a garage door (600 HP)");
		check(helper, PieceDamage.get(level).get(controller) == 2 * RaidDamage.TNT_DAMAGE,
				"it took " + 2 * RaidDamage.TNT_DAMAGE + ", got " + PieceDamage.get(level).get(controller));

		level.explode(null, blast.x, blast.y, blast.z, 4.0F, Level.ExplosionInteraction.TNT);

		for (BlockPos part : GarageDoorBlock.positions(north)) {
			check(helper, level.getBlockState(part).isAir(), "the third TNT takes the whole door, but " + part + " is " + level.getBlockState(part));
		}

		check(helper, Structure.hasWall(level, north), "the armored frame stands");
		helper.succeed();
	}

	@GameTest(structure = PLATFORM)
	public void wallsTakeTheirDoorsWithThem(GameTestHelper helper) {
		ServerLevel level = helper.getLevel();
		Cell c = Cell.at(helper, 7, 7);
		Edge north = frameWithDoor(level, c);
		Edge south = TestBuilds.south(c.x(), c.z(), c.y());
		TestBuilds.wall(level, PieceType.DOORWAY, south);
		BlockPos doorPos = south.pos(1, 1);
		BlockState door = ModBlocks.SHEET_METAL_DOOR.defaultBlockState().setValue(DoorBlock.FACING, Direction.NORTH);
		level.setBlock(doorPos, door.setValue(DoorBlock.HALF, DoubleBlockHalf.LOWER), Block.UPDATE_ALL);
		level.setBlock(doorPos.above(), door.setValue(DoorBlock.HALF, DoubleBlockHalf.UPPER), Block.UPDATE_ALL);

		BuildingOps.destroy(level, new PieceRef.Wall(south));
		check(helper, level.getBlockState(doorPos).isAir() && level.getBlockState(doorPos.above()).isAir(), "the door went with its doorway");

		BuildingOps.destroy(level, new PieceRef.Wall(north));

		for (BlockPos part : GarageDoorBlock.positions(north)) {
			check(helper, level.getBlockState(part).isAir(), "the garage door went with its frame, but " + part + " is " + level.getBlockState(part));
		}

		helper.succeed();
	}

	// --- Upkeep ----------------------------------------------------------------------------------

	@GameTest(structure = PLATFORM)
	public void upkeepUsesRustsBrackets(GameTestHelper helper) {
		checkClose(helper, Upkeep.rate(1), 0.1, "one piece");
		checkClose(helper, Upkeep.rate(15), 0.1, "fifteen pieces");
		checkClose(helper, Upkeep.rate(20), (15 * 0.1 + 5 * 0.15) / 20, "twenty pieces (Corrosion Hour's 11.25% example)");
		checkClose(helper, Upkeep.rate(65), (15 * 0.1 + 50 * 0.15) / 65, "65 pieces");
		checkClose(helper, Upkeep.rate(190), (15 * 0.1 + 50 * 0.15 + 125 * 0.2) / 190, "190 pieces");
		checkClose(helper, Upkeep.rate(200), (15 * 0.1 + 50 * 0.15 + 125 * 0.2 + 10 * 0.333) / 200, "200 pieces");

		Upkeep.Bill bill = Upkeep.bill(List.of(
				new Upkeep.Charge(BuildingTier.STONE, 9),
				new Upkeep.Charge(BuildingTier.STONE, 9),
				new Upkeep.Charge(BuildingTier.TWIG, 9)));
		check(helper, bill.pieces() == 3, "three pieces");
		checkClose(helper, bill.perPeriod(BuildingTier.STONE), 1.8, "stone per period");
		checkClose(helper, bill.perPeriod(BuildingTier.TWIG), 0.9, "sticks per period");
		check(helper, bill.shownCost(BuildingTier.STONE) == 2, "shown rounded up");
		check(helper, bill.ticksPerItem(BuildingTier.ARMORED) == Long.MAX_VALUE, "no armored pieces, nothing to pay");
		helper.succeed();
	}

	@GameTest(structure = PLATFORM)
	public void cupboardsPayAnItemAtATime(GameTestHelper helper) {
		double[] perPeriod = new double[BuildingTier.values().length];
		perPeriod[BuildingTier.STONE.ordinal()] = 2.0;
		Upkeep.Bill bill = new Upkeep.Bill(1, perPeriod);
		long perItem = Upkeep.PERIOD / 2;
		check(helper, bill.ticksPerItem(BuildingTier.STONE) == perItem, "two cobblestone a period: one per half period");

		SimpleContainer storage = new SimpleContainer(ToolCupboardBlockEntity.STORAGE_SIZE);
		storage.setItem(0, new ItemStack(Items.COBBLESTONE, 3));
		UpkeepAccount account = new UpkeepAccount();
		long t0 = 1000;
		account.settle(t0, bill, storage);
		check(helper, account.paidUntil(BuildingTier.STONE) == t0, "a new account counts as paid up to now");

		account.settle(t0 + 1, bill, storage);
		check(helper, account.paidUntil(BuildingTier.STONE) == t0 + perItem && UpkeepAccount.count(storage, BuildingTier.STONE) == 2,
				"one cobblestone bought half a period, paid until " + account.paidUntil(BuildingTier.STONE));

		account.settle(t0 + perItem + 1, bill, storage);
		check(helper, account.paidUntil(BuildingTier.STONE) == t0 + 2 * perItem && UpkeepAccount.count(storage, BuildingTier.STONE) == 1,
				"the next one when that ran out");

		long now = t0 + 3 * perItem + 5;
		account.settle(now, bill, storage);
		check(helper, account.paidUntil(BuildingTier.STONE) == t0 + 3 * perItem, "the last one, and then it runs out");
		check(helper, account.coveredUntil(BuildingTier.STONE, bill, storage, now) == t0 + 3 * perItem, "nothing left to cover more");

		// More material later pays from then on, not for the gap.
		long refill = t0 + 4 * perItem;
		account.settle(refill, bill, storage);
		storage.setItem(0, new ItemStack(Items.COBBLESTONE, 1));
		account.settle(refill + 10, bill, storage);
		check(helper, account.paidUntil(BuildingTier.STONE) == refill + perItem, "a refill pays from the last settlement, got "
				+ account.paidUntil(BuildingTier.STONE));
		check(helper, account.paidUntil(BuildingTier.TWIG) >= refill + 10, "grades with nothing to pay for count as paid");
		helper.succeed();
	}

	@GameTest(structure = WIDE)
	public void cupboardsCountThePiecesInTheirZone(GameTestHelper helper) {
		ServerLevel level = helper.getLevel();
		Cell c = Cell.at(helper, 20, 20);
		TestBuilds.slab(level, PieceType.FOUNDATION, c.x(), c.z(), c.y());
		TestBuilds.slab(level, PieceType.FOUNDATION, c.x() + 1, c.z(), c.y());

		for (Edge edge : Grid.edgesOf(c.x(), c.z(), c.y())) {
			TestBuilds.wall(level, PieceType.WALL, edge);
		}

		BuildingOps.upgrade(level, new PieceRef.Slab(c.x(), c.z(), c.y()), BuildingTier.STONE);
		BlockPos cupboard = Grid.slabAnchor(c.x() + 1, c.z(), c.y()).above();
		level.setBlock(cupboard, ModBlocks.TOOL_CUPBOARD.defaultBlockState(), Block.UPDATE_ALL);

		Upkeep.Bill bill = Upkeep.scan(level, cupboard);
		check(helper, bill.pieces() == 6, "two foundations and four walls, got " + bill.pieces());
		double rate = Upkeep.rate(6);
		checkClose(helper, bill.perPeriod(BuildingTier.STONE), 9 * rate, "the stone foundation");
		checkClose(helper, bill.perPeriod(BuildingTier.TWIG), (9 + 4 * 9) * rate, "the twig foundation and walls");
		helper.succeed();
	}

	// --- Decay -----------------------------------------------------------------------------------

	@GameTest(structure = PLATFORM)
	public void decayTakesRustsHoursAsMinecraftDays(GameTestHelper helper) {
		long[] rustHours = {1, 3, 5, 8, 12};

		for (BuildingTier tier : BuildingTier.values()) {
			long duration = rustHours[tier.ordinal()] * Upkeep.RUST_HOUR;
			check(helper, Decay.duration(tier) == duration, tier + " decays in " + rustHours[tier.ordinal()] + " Minecraft days");
			check(helper, Decay.ticksPerPoint(tier) * tier.health() == duration, tier + "'s health divides its decay time evenly");
		}

		check(helper, Upkeep.PERIOD == 24 * Upkeep.RUST_HOUR, "upkeep is charged every 24 Rust hours");
		helper.succeed();
	}

	@GameTest(structure = WIDE)
	public void piecesWithoutUpkeepDecay(GameTestHelper helper) {
		ServerLevel level = helper.getLevel();
		Cell c = Cell.at(helper, 20, 20);
		TestBuilds.slab(level, PieceType.FOUNDATION, c.x(), c.z(), c.y());
		PieceRef.Slab slab = new PieceRef.Slab(c.x(), c.z(), c.y());
		long t0 = level.getGameTime();
		check(helper, Decay.protection(level, slab.anchor(), BuildingTier.TWIG, t0).status() == Decay.Status.DECAYING,
				"no cupboard: decaying (and the area around is loaded)");

		Decay.apply(level, slab, t0);
		check(helper, PieceDamage.get(level).get(slab.anchor()) == 0, "the first look only starts the clock");

		long perPoint = Decay.ticksPerPoint(BuildingTier.TWIG);
		Decay.apply(level, slab, t0 + 5 * perPoint + 7);
		check(helper, PieceDamage.get(level).get(slab.anchor()) == 5, "half a day takes half of twig's 10 HP, got "
				+ PieceDamage.get(level).get(slab.anchor()));
		check(helper, !Decay.apply(level, slab, t0 + 5 * perPoint + 7) && PieceDamage.get(level).get(slab.anchor()) == 5,
				"no time passed, no more damage");

		check(helper, Decay.apply(level, slab, t0 + 10 * perPoint), "a day without upkeep and twig falls");
		check(helper, !Structure.slabExists(level, c.x(), c.z(), c.y()), "the foundation is gone");
		check(helper, DecayClocks.get(level).get(slab.anchor()) == DecayClocks.NONE, "and so is its decay clock");
		helper.succeed();
	}

	@GameTest(structure = WIDE)
	public void paidUpkeepStopsDecay(GameTestHelper helper) {
		ServerLevel level = helper.getLevel();
		Cell c = Cell.at(helper, 20, 20);
		TestBuilds.slab(level, PieceType.FOUNDATION, c.x(), c.z(), c.y());
		PieceRef.Slab slab = new PieceRef.Slab(c.x(), c.z(), c.y());
		BlockPos cupboardPos = Grid.slabAnchor(c.x(), c.z(), c.y()).above();
		level.setBlock(cupboardPos, ModBlocks.TOOL_CUPBOARD.defaultBlockState(), Block.UPDATE_ALL);
		ToolCupboardBlockEntity cupboard = (ToolCupboardBlockEntity) level.getBlockEntity(cupboardPos);
		cupboard.storage().setItem(0, new ItemStack(Items.STICK, 2));

		long t0 = level.getGameTime();
		Decay.apply(level, slab, t0);
		check(helper, Decay.protection(level, slab.anchor(), BuildingTier.TWIG, t0).status() == Decay.Status.PROTECTED,
				"a new cupboard counts as paid up to now");

		// One twig foundation: 9 sticks x 10% = 0.9 sticks every 24 days, so a stick lasts about 26.7 days.
		Decay.apply(level, slab, t0 + 20 * Upkeep.RUST_HOUR);
		check(helper, PieceDamage.get(level).get(slab.anchor()) == 0, "paid upkeep: no decay");
		check(helper, UpkeepAccount.count(cupboard.storage(), BuildingTier.TWIG) == 1, "one stick paid for those 20 days");
		long lapsed = cupboard.paidUntil(BuildingTier.TWIG);
		check(helper, lapsed == t0 + cupboard.bill().ticksPerItem(BuildingTier.TWIG), "paid until a stick's worth after the start, got " + (lapsed - t0));

		cupboard.storage().clearContent();
		Decay.apply(level, slab, lapsed + 2 * Decay.ticksPerPoint(BuildingTier.TWIG));
		check(helper, PieceDamage.get(level).get(slab.anchor()) == 2, "decay counts from when the upkeep ran out, got "
				+ PieceDamage.get(level).get(slab.anchor()));
		helper.succeed();
	}
}
