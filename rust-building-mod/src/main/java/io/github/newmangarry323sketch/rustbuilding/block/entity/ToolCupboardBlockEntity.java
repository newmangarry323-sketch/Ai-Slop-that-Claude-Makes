package io.github.newmangarry323sketch.rustbuilding.block.entity;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.UUID;

import com.mojang.serialization.Codec;

import net.minecraft.core.BlockPos;
import net.minecraft.core.HolderLookup;
import net.minecraft.core.NonNullList;
import net.minecraft.nbt.CompoundTag;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.Container;
import net.minecraft.world.ContainerHelper;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.storage.ValueInput;
import net.minecraft.world.level.storage.ValueOutput;

import io.github.newmangarry323sketch.rustbuilding.building.BuildingTier;
import io.github.newmangarry323sketch.rustbuilding.lock.CodeLock;
import io.github.newmangarry323sketch.rustbuilding.lock.LockHolder;
import io.github.newmangarry323sketch.rustbuilding.lock.Member;
import io.github.newmangarry323sketch.rustbuilding.registry.ModBlockEntities;
import io.github.newmangarry323sketch.rustbuilding.upkeep.Upkeep;
import io.github.newmangarry323sketch.rustbuilding.upkeep.UpkeepAccount;

/**
 * Holds the building privilege list, and the materials that pay the base's upkeep. As in Rust, anyone
 * who can reach the cupboard can add themselves, clear the list or take the materials, which is why you
 * lock it.
 */
public class ToolCupboardBlockEntity extends SyncedBlockEntity implements LockHolder {
	public static final int STORAGE_SIZE = 9;
	private static final int SETTLE_INTERVAL = 20;
	/** How often the pieces in the zone are counted again; building and upgrading show up this late. */
	private static final int RESCAN_INTERVAL = 600;
	private static final long NEVER = Long.MIN_VALUE;
	private static final String ITEMS_KEY = "Items";
	private static final int TIERS = BuildingTier.values().length;

	private final List<Member> authorized = new ArrayList<>();
	private final CodeLock lock = new CodeLock();
	private final NonNullList<ItemStack> items = NonNullList.withSize(STORAGE_SIZE, ItemStack.EMPTY);
	private final Storage storage = new Storage();
	private final UpkeepAccount account = new UpkeepAccount();
	private Upkeep.Bill bill = Upkeep.Bill.NONE;
	private long lastScan = NEVER;

	// What clients are shown: the size of the base, the upkeep per period and how long each grade is
	// covered for. Recomputed on the server; sent only when it changes.
	private int shownPieces;
	private int[] shownCost = new int[TIERS];
	private long[] shownCoveredUntil = filled(Long.MAX_VALUE);

	public ToolCupboardBlockEntity(BlockPos pos, BlockState state) {
		super(ModBlockEntities.TOOL_CUPBOARD, pos, state);
	}

	// --- Building privilege --------------------------------------------------------------------

	public List<Member> authorized() {
		return Collections.unmodifiableList(this.authorized);
	}

	public boolean isAuthorized(UUID id) {
		for (Member member : this.authorized) {
			if (member.id().equals(id)) {
				return true;
			}
		}

		return false;
	}

	public void authorize(Player player) {
		if (!this.isAuthorized(player.getUUID())) {
			this.authorized.add(Member.of(player));
			this.sync();
		}
	}

	public void deauthorize(Player player) {
		if (this.authorized.removeIf(member -> member.id().equals(player.getUUID()))) {
			this.sync();
		}
	}

	public void clearAuthorized() {
		this.authorized.clear();
		this.sync();
	}

	@Override
	public CodeLock lock() {
		return this.lock;
	}

	@Override
	public void lockChanged() {
		this.sync();
	}

	// --- Upkeep --------------------------------------------------------------------------------

	/** The upkeep materials, as a container for the storage screen. */
	public Container storage() {
		return this.storage;
	}

	public void serverTick(ServerLevel level) {
		if (level.getGameTime() % SETTLE_INTERVAL == Math.floorMod(this.worldPosition.asLong(), SETTLE_INTERVAL)) {
			this.settle(level, level.getGameTime());
		}
	}

	/**
	 * Pays upkeep up to {@code now}, counting the zone's pieces again first if that is due. Cheap to
	 * call often: anything that needs to know whether the base is protected calls it first.
	 */
	public void settle(ServerLevel level, long now) {
		if (this.lastScan == NEVER || now - this.lastScan >= RESCAN_INTERVAL || now < this.lastScan) {
			this.bill = Upkeep.scan(level, this.worldPosition);
			this.lastScan = now;
		}

		if (this.account.settle(now, this.bill, this.storage)) {
			this.setChanged();
		}

		this.updateShown(now);
	}

	/** The game time this grade's pieces are paid up to. */
	public long paidUntil(BuildingTier tier) {
		return this.account.paidUntil(tier);
	}

	public Upkeep.Bill bill() {
		return this.bill;
	}

	public int shownPieces() {
		return this.shownPieces;
	}

	/** Whole items of a grade's material charged per upkeep period. */
	public int shownCost(BuildingTier tier) {
		return this.shownCost[tier.ordinal()];
	}

	/** The game time a grade's protection runs out, or {@link Long#MAX_VALUE} if nothing of that grade is charged. */
	public long shownCoveredUntil(BuildingTier tier) {
		return this.shownCoveredUntil[tier.ordinal()];
	}

	private void updateShown(long now) {
		int[] cost = new int[TIERS];
		long[] coveredUntil = new long[TIERS];

		for (BuildingTier tier : BuildingTier.values()) {
			cost[tier.ordinal()] = this.bill.shownCost(tier);
			coveredUntil[tier.ordinal()] = this.account.coveredUntil(tier, this.bill, this.storage, now);
		}

		if (this.bill.pieces() != this.shownPieces || !Arrays.equals(cost, this.shownCost) || !Arrays.equals(coveredUntil, this.shownCoveredUntil)) {
			this.shownPieces = this.bill.pieces();
			this.shownCost = cost;
			this.shownCoveredUntil = coveredUntil;
			this.sync();
		}
	}

	private static long[] filled(long value) {
		long[] array = new long[TIERS];
		Arrays.fill(array, value);
		return array;
	}

	// --- Saving --------------------------------------------------------------------------------

	@Override
	protected void saveAdditional(ValueOutput output) {
		super.saveAdditional(output);
		output.store("authorized", Member.CODEC.listOf(), List.copyOf(this.authorized));
		this.lock.save(output);
		ContainerHelper.saveAllItems(output, this.items);
		this.account.save(output);

		ValueOutput shown = output.child("upkeep_shown");
		shown.putInt("pieces", this.shownPieces);
		shown.store("cost", Codec.INT.listOf(), Arrays.stream(this.shownCost).boxed().toList());
		shown.store("covered_until", Codec.LONG.listOf(), Arrays.stream(this.shownCoveredUntil).boxed().toList());
	}

	@Override
	protected void loadAdditional(ValueInput input) {
		super.loadAdditional(input);
		this.authorized.clear();
		this.authorized.addAll(input.read("authorized", Member.CODEC.listOf()).orElse(List.of()));
		this.lock.load(input);
		this.items.clear();
		ContainerHelper.loadAllItems(input, this.items);
		this.account.load(input);

		ValueInput shown = input.childOrEmpty("upkeep_shown");
		this.shownPieces = shown.getIntOr("pieces", 0);
		List<Integer> cost = shown.read("cost", Codec.INT.listOf()).orElse(List.of());
		List<Long> coveredUntil = shown.read("covered_until", Codec.LONG.listOf()).orElse(List.of());

		for (int i = 0; i < TIERS; i++) {
			this.shownCost[i] = i < cost.size() ? cost.get(i) : 0;
			this.shownCoveredUntil[i] = i < coveredUntil.size() ? coveredUntil.get(i) : Long.MAX_VALUE;
		}
	}

	/** Clients are told how long the base is covered, but not what is in the storage. */
	@Override
	public CompoundTag getUpdateTag(HolderLookup.Provider registries) {
		CompoundTag tag = super.getUpdateTag(registries);
		tag.remove(ITEMS_KEY);
		return tag;
	}

	/** The cupboard's nine slots, as the storage screen sees them. */
	private final class Storage implements Container {
		@Override
		public int getContainerSize() {
			return STORAGE_SIZE;
		}

		@Override
		public boolean isEmpty() {
			for (ItemStack stack : ToolCupboardBlockEntity.this.items) {
				if (!stack.isEmpty()) {
					return false;
				}
			}

			return true;
		}

		@Override
		public ItemStack getItem(int slot) {
			return ToolCupboardBlockEntity.this.items.get(slot);
		}

		@Override
		public ItemStack removeItem(int slot, int count) {
			ItemStack removed = ContainerHelper.removeItem(ToolCupboardBlockEntity.this.items, slot, count);

			if (!removed.isEmpty()) {
				this.setChanged();
			}

			return removed;
		}

		@Override
		public ItemStack removeItemNoUpdate(int slot) {
			return ContainerHelper.takeItem(ToolCupboardBlockEntity.this.items, slot);
		}

		@Override
		public void setItem(int slot, ItemStack stack) {
			ToolCupboardBlockEntity.this.items.set(slot, stack);
			stack.limitSize(this.getMaxStackSize(stack));
			this.setChanged();
		}

		@Override
		public void setChanged() {
			ToolCupboardBlockEntity.this.setChanged();
		}

		@Override
		public boolean stillValid(Player player) {
			return Container.stillValidBlockEntity(ToolCupboardBlockEntity.this, player);
		}

		@Override
		public void clearContent() {
			ToolCupboardBlockEntity.this.items.clear();
			this.setChanged();
		}
	}
}
