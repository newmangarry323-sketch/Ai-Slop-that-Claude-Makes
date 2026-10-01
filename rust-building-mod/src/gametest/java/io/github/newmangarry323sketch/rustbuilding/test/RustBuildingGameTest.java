package io.github.newmangarry323sketch.rustbuilding.test;

import java.util.List;

import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.gametest.framework.GameTestHelper;
import net.minecraft.network.chat.Component;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.level.GameType;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.DoorBlock;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.block.state.properties.DoubleBlockHalf;
import net.minecraft.world.phys.Vec3;

import net.fabricmc.fabric.api.gametest.v1.GameTest;

import io.github.newmangarry323sketch.rustbuilding.block.entity.ToolCupboardBlockEntity;
import io.github.newmangarry323sketch.rustbuilding.building.BuildingKind;
import io.github.newmangarry323sketch.rustbuilding.building.BuildingOps;
import io.github.newmangarry323sketch.rustbuilding.building.BuildingTier;
import io.github.newmangarry323sketch.rustbuilding.building.Edge;
import io.github.newmangarry323sketch.rustbuilding.building.Grid;
import io.github.newmangarry323sketch.rustbuilding.building.PiecePlanner;
import io.github.newmangarry323sketch.rustbuilding.building.PieceRef;
import io.github.newmangarry323sketch.rustbuilding.building.PieceType;
import io.github.newmangarry323sketch.rustbuilding.building.PlannedPiece;
import io.github.newmangarry323sketch.rustbuilding.building.Structure;
import io.github.newmangarry323sketch.rustbuilding.lock.CodeLock;
import io.github.newmangarry323sketch.rustbuilding.lock.LockHolder;
import io.github.newmangarry323sketch.rustbuilding.privilege.BuildingPrivilege;
import io.github.newmangarry323sketch.rustbuilding.raid.PieceDamage;
import io.github.newmangarry323sketch.rustbuilding.raid.RaidDamage;
import io.github.newmangarry323sketch.rustbuilding.registry.ModBlocks;

/**
 * Server game tests: each runs in a fresh 20 x 12 x 20 area with a stone floor, builds through the same
 * planner and operations the items use, and checks the blocks that end up in the world.
 */
public class RustBuildingGameTest {
	private static final String PLATFORM = "rustbuilding-test:platform";

	/** A grid cell near the middle of the platform, in world coordinates, with its slab level. */
	private record Cell(int x, int z, int y) {
		static Cell middle(GameTestHelper helper) {
			BlockPos middle = helper.absolutePos(new BlockPos(7, 1, 7));
			return new Cell(Grid.cell(middle.getX()), Grid.cell(middle.getZ()), middle.getY());
		}
	}

	private static void check(GameTestHelper helper, boolean condition, String message) {
		helper.assertTrue(condition, Component.literal(message));
	}

	private static void checkPiece(GameTestHelper helper, BlockPos pos, BuildingKind kind, BuildingTier tier) {
		BlockState state = helper.getLevel().getBlockState(pos);
		check(helper, Structure.kindOf(state) == kind && Structure.tierOf(state) == tier,
				"expected " + tier + " " + kind + " at " + pos + " but found " + state);
	}

	private static void checkAir(GameTestHelper helper, BlockPos pos) {
		BlockState state = helper.getLevel().getBlockState(pos);
		check(helper, state.isAir(), "expected air at " + pos + " but found " + state);
	}

	@GameTest(structure = PLATFORM)
	public void foundationsShareTheLineBetweenThem(GameTestHelper helper) {
		ServerLevel level = helper.getLevel();
		Cell c = Cell.middle(helper);
		PlannedPiece first = TestBuilds.slab(level, PieceType.FOUNDATION, c.x(), c.z(), c.y());
		check(helper, first.positions().size() == 25, "a lone foundation is 5 x 5 blocks, got " + first.positions().size());

		for (BlockPos pos : Grid.slabInterior(c.x(), c.z(), c.y())) {
			checkPiece(helper, pos, BuildingKind.SLAB, BuildingTier.TWIG);
		}

		for (BlockPos pos : Grid.slabBorder(c.x(), c.z(), c.y())) {
			checkPiece(helper, pos, BuildingKind.SLAB, BuildingTier.TWIG);
		}

		PlannedPiece second = TestBuilds.slab(level, PieceType.FOUNDATION, c.x() + 1, c.z(), c.y());
		check(helper, second.positions().size() == 20, "the shared line of 5 blocks is reused, so 20 new blocks, got " + second.positions().size());
		check(helper, second.units() == 9, "a foundation costs 9 units");
		helper.succeed();
	}

	@GameTest(structure = PLATFORM)
	public void wallsStandOnEdgesAndShareCornerPillars(GameTestHelper helper) {
		ServerLevel level = helper.getLevel();
		Cell c = Cell.middle(helper);
		TestBuilds.slab(level, PieceType.FOUNDATION, c.x(), c.z(), c.y());

		Edge north = TestBuilds.north(c.x(), c.z(), c.y());
		TestBuilds.wall(level, PieceType.WALL, north);

		for (int u = 0; u < 3; u++) {
			for (int h = 1; h <= 3; h++) {
				checkPiece(helper, north.pos(u, h), BuildingKind.WALL, BuildingTier.TWIG);
			}
		}

		for (int end = 0; end <= 1; end++) {
			for (int h = 1; h <= 3; h++) {
				checkPiece(helper, north.corner(end, h), BuildingKind.WALL, BuildingTier.TWIG);
			}
		}

		Edge east = TestBuilds.east(c.x(), c.z(), c.y());
		PlannedPiece doorway = TestBuilds.wall(level, PieceType.DOORWAY, east);
		check(helper, doorway.units() == 7, "a doorway is 7 blocks");
		checkAir(helper, east.pos(1, 1));
		checkAir(helper, east.pos(1, 2));
		checkPiece(helper, east.pos(1, 3), BuildingKind.WALL, BuildingTier.TWIG);
		check(helper, new PieceRef.Wall(east).type(level) == PieceType.DOORWAY, "the doorway is recognised as one");

		// Nothing to stand on two cells away, and no doubling up on an edge.
		Edge nowhere = TestBuilds.south(c.x(), c.z() + 1, c.y());
		check(helper, !PiecePlanner.planAt(level, null, PieceType.WALL, new PiecePlanner.EdgeTarget(nowhere)).valid(),
				"a wall without a foundation edge under it is refused");
		check(helper, !PiecePlanner.planAt(level, null, PieceType.WALL, new PiecePlanner.EdgeTarget(north)).valid(),
				"a second wall on the same edge is refused");
		helper.succeed();
	}

	@GameTest(structure = PLATFORM)
	public void aimingPicksTheNearestEdge(GameTestHelper helper) {
		Cell c = Cell.middle(helper);
		BlockPos centre = Grid.slabAnchor(c.x(), c.z(), c.y());
		BlockPos nearNorth = centre.north();
		Vec3 hitNearNorth = new Vec3(nearNorth.getX() + 0.5, nearNorth.getY() + 1, nearNorth.getZ() + 0.1);
		check(helper, PiecePlanner.nearestEdge(nearNorth, hitNearNorth).equals(TestBuilds.north(c.x(), c.z(), c.y())),
				"aiming at the north side of a foundation targets its north edge");
		BlockPos nearEast = centre.east();
		Vec3 hitNearEast = new Vec3(nearEast.getX() + 0.9, nearEast.getY() + 1, nearEast.getZ() + 0.5);
		check(helper, PiecePlanner.nearestEdge(nearEast, hitNearEast).equals(TestBuilds.east(c.x(), c.z(), c.y())),
				"aiming at the east side of a foundation targets its east edge");
		helper.succeed();
	}

	@GameTest(structure = PLATFORM)
	public void upgradesKeepSharedBlocksAtTheBestGrade(GameTestHelper helper) {
		ServerLevel level = helper.getLevel();
		Cell c = Cell.middle(helper);
		TestBuilds.slab(level, PieceType.FOUNDATION, c.x(), c.z(), c.y());
		TestBuilds.slab(level, PieceType.FOUNDATION, c.x() + 1, c.z(), c.y());
		BuildingOps.upgrade(level, new PieceRef.Slab(c.x(), c.z(), c.y()), BuildingTier.STONE);

		checkPiece(helper, Grid.slabAnchor(c.x(), c.z(), c.y()), BuildingKind.SLAB, BuildingTier.STONE);
		BlockPos sharedLine = new BlockPos(Grid.lineOf(c.x() + 1), c.y(), Grid.lineOf(c.z()) + 2);
		checkPiece(helper, sharedLine, BuildingKind.SLAB, BuildingTier.STONE);
		checkPiece(helper, Grid.slabAnchor(c.x() + 1, c.z(), c.y()), BuildingKind.SLAB, BuildingTier.TWIG);
		BlockPos farLine = new BlockPos(Grid.lineOf(c.x() + 2), c.y(), Grid.lineOf(c.z()) + 2);
		checkPiece(helper, farLine, BuildingKind.SLAB, BuildingTier.TWIG);
		check(helper, new PieceRef.Slab(c.x(), c.z(), c.y()).tier(level) == BuildingTier.STONE, "the piece reports its grade");
		helper.succeed();
	}

	@GameTest(structure = PLATFORM)
	public void removingAFoundationBringsDownWhatRestsOnIt(GameTestHelper helper) {
		ServerLevel level = helper.getLevel();
		Cell c = Cell.middle(helper);
		TestBuilds.slab(level, PieceType.FOUNDATION, c.x(), c.z(), c.y());

		for (Edge edge : Grid.edgesOf(c.x(), c.z(), c.y())) {
			TestBuilds.wall(level, PieceType.WALL, edge);
		}

		int upper = c.y() + Grid.STOREY;
		TestBuilds.slab(level, PieceType.FLOOR, c.x(), c.z(), upper);
		check(helper, Structure.slabExists(level, c.x(), c.z(), upper), "the floor is placed on the walls");

		int destroyed = BuildingOps.destroy(level, new PieceRef.Slab(c.x(), c.z(), c.y()));
		check(helper, destroyed == 6, "foundation, four walls and the floor come down, got " + destroyed);

		for (Edge edge : Grid.edgesOf(c.x(), c.z(), c.y())) {
			check(helper, !Structure.hasWall(level, edge), "no wall left on " + edge);
		}

		check(helper, !Structure.slabExists(level, c.x(), c.z(), upper), "the floor fell");
		checkAir(helper, Grid.slabAnchor(c.x(), c.z(), c.y()));
		checkAir(helper, TestBuilds.north(c.x(), c.z(), c.y()).corner(0, 2));
		helper.succeed();
	}

	@GameTest(structure = PLATFORM)
	public void stairsClimbToTheNextStorey(GameTestHelper helper) {
		ServerLevel level = helper.getLevel();
		Cell c = Cell.middle(helper);
		TestBuilds.slab(level, PieceType.FOUNDATION, c.x(), c.z(), c.y());
		PlannedPiece stairs = TestBuilds.place(level, PieceType.STAIRS, new PiecePlanner.StairsTarget(c.x(), c.z(), c.y(), Direction.NORTH));
		PieceRef.Stairs ref = new PieceRef.Stairs(c.x(), c.z(), c.y());
		check(helper, ref.ownBlocks(level).size() == 4, "four steps");
		check(helper, stairs.units() == 8, "stairs cost 8 units");

		int highest = Integer.MIN_VALUE;

		for (BlockPos pos : ref.ownBlocks(level)) {
			highest = Math.max(highest, pos.getY());
		}

		check(helper, highest == c.y() + Grid.STOREY, "the top step is level with the next floor");
		check(helper, !PiecePlanner.planAt(level, null, PieceType.STAIRS, new PiecePlanner.StairsTarget(c.x(), c.z(), c.y(), Direction.EAST)).valid(),
				"two staircases cannot share a cell");
		helper.succeed();
	}

	@GameTest(structure = PLATFORM)
	public void cupboardsGrantBuildingPrivilege(GameTestHelper helper) {
		ServerLevel level = helper.getLevel();
		Player owner = helper.makeMockPlayer(GameType.SURVIVAL);
		Player stranger = helper.makeMockPlayer(GameType.SURVIVAL);
		check(helper, !owner.getUUID().equals(stranger.getUUID()), "mock players need distinct ids");

		BlockPos cupboardPos = helper.absolutePos(new BlockPos(2, 1, 2));
		level.setBlock(cupboardPos, ModBlocks.TOOL_CUPBOARD.defaultBlockState(), Block.UPDATE_ALL);
		check(helper, level.getBlockEntity(cupboardPos) instanceof ToolCupboardBlockEntity, "the cupboard has a block entity");
		ToolCupboardBlockEntity cupboard = (ToolCupboardBlockEntity) level.getBlockEntity(cupboardPos);
		cupboard.authorize(owner);

		BlockPos inside = cupboardPos.offset(5, 0, 5);
		BlockPos outside = cupboardPos.offset(BuildingPrivilege.RANGE + 3, 0, 0);
		check(helper, BuildingPrivilege.canBuild(level, owner, inside), "the owner may build near their cupboard");
		check(helper, !BuildingPrivilege.canBuild(level, stranger, inside), "a stranger may not");
		check(helper, BuildingPrivilege.canBuild(level, stranger, outside), "outside the zone anyone may build");
		check(helper, !BuildingPrivilege.canPlaceCupboard(level, stranger, outside), "a stranger's cupboard may not overlap the zone");

		cupboard.clearAuthorized();
		check(helper, !BuildingPrivilege.canBuild(level, owner, inside), "clearing the list removes the owner too, as in Rust");
		helper.succeed();
	}

	@GameTest(structure = PLATFORM)
	public void codeLocksRememberWhoKnowsTheCode(GameTestHelper helper) {
		Player owner = helper.makeMockPlayer(GameType.SURVIVAL);
		Player friend = helper.makeMockPlayer(GameType.SURVIVAL);
		CodeLock lock = new CodeLock();
		check(helper, lock.canAccess(friend), "no lock, no restriction");

		lock.install(owner, "1234");
		check(helper, lock.canAccess(owner) && !lock.canAccess(friend), "only the owner gets in at first");
		check(helper, !lock.tryCode(friend, "0000"), "a wrong code is refused");
		check(helper, lock.tryCode(friend, "1234") && lock.canAccess(friend), "the right code is remembered");

		lock.changeCode("4321");
		check(helper, lock.canAccess(owner) && !lock.canAccess(friend), "changing the code forgets everyone but the owner");
		check(helper, CodeLock.isValidCode("0042") && !CodeLock.isValidCode("12a4") && !CodeLock.isValidCode("123"), "codes are four digits");
		helper.succeed();
	}

	@GameTest(structure = PLATFORM)
	public void doorsKeepTheirLockOnTheBottomHalf(GameTestHelper helper) {
		ServerLevel level = helper.getLevel();
		BlockPos lower = helper.absolutePos(new BlockPos(5, 1, 5));
		BlockState door = ModBlocks.SHEET_METAL_DOOR.defaultBlockState();
		level.setBlock(lower, door.setValue(DoorBlock.HALF, DoubleBlockHalf.LOWER), Block.UPDATE_ALL);
		level.setBlock(lower.above(), door.setValue(DoorBlock.HALF, DoubleBlockHalf.UPPER), Block.UPDATE_ALL);

		LockHolder fromTop = LockHolder.find(level, lower.above());
		check(helper, fromTop != null && fromTop.getBlockPos().equals(lower), "the top half finds the bottom half's lock");
		check(helper, level.getBlockEntity(lower.above()) == null, "the top half has no block entity of its own");
		helper.succeed();
	}

	@GameTest(structure = PLATFORM, maxTicks = 40)
	public void explosionsWearPiecesDownByGrade(GameTestHelper helper) {
		ServerLevel level = helper.getLevel();
		Cell c = Cell.middle(helper);
		TestBuilds.slab(level, PieceType.FOUNDATION, c.x(), c.z(), c.y());
		Edge north = TestBuilds.north(c.x(), c.z(), c.y());
		TestBuilds.wall(level, PieceType.WALL, north);
		// Armored foundation so only the stone wall is at risk.
		BuildingOps.upgrade(level, new PieceRef.Slab(c.x(), c.z(), c.y()), BuildingTier.ARMORED);
		PieceRef.Wall wall = new PieceRef.Wall(north);
		BuildingOps.upgrade(level, wall, BuildingTier.STONE);

		// A TNT-sized blast right outside the middle of the wall.
		Vec3 blast = north.pos(1, 2).north().getCenter();
		level.explode(null, blast.x, blast.y, blast.z, 4.0F, Level.ExplosionInteraction.TNT);
		check(helper, Structure.hasWall(level, north), "one TNT does not break stone (500 HP)");
		check(helper, PieceDamage.get(level).get(wall.anchor()) == RaidDamage.TNT_DAMAGE,
				"the wall took " + RaidDamage.TNT_DAMAGE + ", got " + PieceDamage.get(level).get(wall.anchor()));

		level.explode(null, blast.x, blast.y, blast.z, 4.0F, Level.ExplosionInteraction.TNT);
		check(helper, !Structure.hasWall(level, north), "two TNT break stone");
		check(helper, Structure.slabExists(level, c.x(), c.z(), c.y()), "the armored foundation survives");
		helper.succeed();
	}

	@GameTest(structure = PLATFORM, maxTicks = 40)
	public void twigFallsToAnyBlast(GameTestHelper helper) {
		ServerLevel level = helper.getLevel();
		Cell c = Cell.middle(helper);
		TestBuilds.slab(level, PieceType.FOUNDATION, c.x(), c.z(), c.y());
		Edge north = TestBuilds.north(c.x(), c.z(), c.y());
		TestBuilds.wall(level, PieceType.WALL, north);

		Vec3 blast = north.pos(1, 2).north().getCenter();
		level.explode(null, blast.x, blast.y, blast.z, 4.0F, Level.ExplosionInteraction.TNT);
		checkAir(helper, north.pos(1, 2));
		helper.succeed();
	}

	@GameTest(structure = PLATFORM, maxTicks = 40)
	public void sheetMetalDoorsFallToOneTnt(GameTestHelper helper) {
		ServerLevel level = helper.getLevel();
		BlockPos lower = helper.absolutePos(new BlockPos(9, 1, 9));
		BlockState door = ModBlocks.SHEET_METAL_DOOR.defaultBlockState();
		level.setBlock(lower, door.setValue(DoorBlock.HALF, DoubleBlockHalf.LOWER), Block.UPDATE_ALL);
		level.setBlock(lower.above(), door.setValue(DoorBlock.HALF, DoubleBlockHalf.UPPER), Block.UPDATE_ALL);

		Vec3 blast = lower.north().getCenter();
		level.explode(null, blast.x, blast.y, blast.z, 4.0F, Level.ExplosionInteraction.TNT);
		checkAir(helper, lower);
		checkAir(helper, lower.above());
		helper.succeed();
	}

	@GameTest(structure = PLATFORM)
	public void demoBaseBuilds(GameTestHelper helper) {
		Cell c = Cell.middle(helper);
		TestBuilds.demoBase(helper.getLevel(), c.x() - 1, c.z() - 1, c.y());
		List<PieceRef> checks = List.of(
				new PieceRef.Slab(c.x() - 1, c.z(), c.y()),
				new PieceRef.Wall(TestBuilds.south(c.x() - 1, c.z(), c.y())),
				new PieceRef.Stairs(c.x() - 1, c.z() - 1, c.y()));
		List<BuildingTier> expected = List.of(BuildingTier.ARMORED, BuildingTier.STONE, BuildingTier.WOOD);

		for (int i = 0; i < checks.size(); i++) {
			check(helper, checks.get(i).tier(helper.getLevel()) == expected.get(i), checks.get(i) + " should be " + expected.get(i));
		}

		helper.succeed();
	}
}
