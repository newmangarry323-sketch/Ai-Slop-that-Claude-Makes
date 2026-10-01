package io.github.newmangarry323sketch.rustbuilding.upkeep;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import com.mojang.serialization.Codec;

import net.minecraft.world.Container;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.level.storage.ValueInput;
import net.minecraft.world.level.storage.ValueOutput;

import io.github.newmangarry323sketch.rustbuilding.building.BuildingTier;

/**
 * A tool cupboard's upkeep payments: for each grade, the game time its pieces are paid up to.
 *
 * <p>The cupboard pays a whole item at a time, so it is usually paid a little ahead. Time is only
 * bought from the last settlement onwards: when a grade's material runs out, that grade stays unpaid -
 * and its pieces decay - until more is put in, and the gap is never paid for afterwards.
 */
public final class UpkeepAccount {
	private static final String KEY = "upkeep";
	private static final long NEVER = Long.MIN_VALUE;

	private final long[] paidUntil = new long[BuildingTier.values().length];
	private long lastSettled = NEVER;

	/** The game time this grade is paid up to; protection lapsed if it is in the past. */
	public long paidUntil(BuildingTier tier) {
		return this.lastSettled == NEVER ? NEVER : this.paidUntil[tier.ordinal()];
	}

	public boolean settled() {
		return this.lastSettled != NEVER;
	}

	/**
	 * Pays what is due up to {@code now} out of {@code storage}. A grade the bill does not charge for is
	 * simply paid up. The first settlement opens the account: everything counts as paid up to then.
	 *
	 * @return whether anything was taken from the storage
	 */
	public boolean settle(long now, Upkeep.Bill bill, Container storage) {
		if (this.lastSettled == NEVER) {
			Arrays.fill(this.paidUntil, now);
			this.lastSettled = now;
			return false;
		}

		if (now <= this.lastSettled) {
			return false;
		}

		boolean took = false;

		for (BuildingTier tier : BuildingTier.values()) {
			int index = tier.ordinal();

			if (bill.perPeriod(tier) <= 0.0) {
				this.paidUntil[index] = Math.max(this.paidUntil[index], now);
				continue;
			}

			long ticksPerItem = bill.ticksPerItem(tier);
			long start = Math.max(this.paidUntil[index], this.lastSettled);
			long covered = start;

			while (covered < now && takeOne(storage, tier)) {
				covered += ticksPerItem;
				took = true;
			}

			if (covered != start) {
				this.paidUntil[index] = covered;
			}
		}

		this.lastSettled = now;
		return took;
	}

	/**
	 * When this grade's protection runs out if nothing more is put in or taken out: what is paid already
	 * plus what the material in storage will buy.
	 */
	public long coveredUntil(BuildingTier tier, Upkeep.Bill bill, Container storage, long now) {
		if (bill.perPeriod(tier) <= 0.0) {
			return Long.MAX_VALUE;
		}

		long paid = this.paidUntil(tier);
		int stored = count(storage, tier);

		if (stored == 0) {
			return paid;
		}

		long ticksPerItem = bill.ticksPerItem(tier);
		long start = Math.max(paid, now);
		return stored >= (Long.MAX_VALUE - start) / ticksPerItem ? Long.MAX_VALUE : start + stored * ticksPerItem;
	}

	public static int count(Container storage, BuildingTier tier) {
		int total = 0;

		for (int slot = 0; slot < storage.getContainerSize(); slot++) {
			ItemStack stack = storage.getItem(slot);

			if (!stack.isEmpty() && tier.accepts(stack)) {
				total += stack.getCount();
			}
		}

		return total;
	}

	private static boolean takeOne(Container storage, BuildingTier tier) {
		for (int slot = 0; slot < storage.getContainerSize(); slot++) {
			ItemStack stack = storage.getItem(slot);

			if (!stack.isEmpty() && tier.accepts(stack)) {
				storage.removeItem(slot, 1);
				return true;
			}
		}

		return false;
	}

	public void save(ValueOutput output) {
		ValueOutput upkeep = output.child(KEY);
		upkeep.putLong("settled", this.lastSettled);
		List<Long> paid = new ArrayList<>(this.paidUntil.length);

		for (long value : this.paidUntil) {
			paid.add(value);
		}

		upkeep.store("paid_until", Codec.LONG.listOf(), paid);
	}

	public void load(ValueInput input) {
		ValueInput upkeep = input.childOrEmpty(KEY);
		this.lastSettled = upkeep.getLongOr("settled", NEVER);
		List<Long> paid = upkeep.read("paid_until", Codec.LONG.listOf()).orElse(List.of());

		for (int i = 0; i < this.paidUntil.length; i++) {
			this.paidUntil[i] = i < paid.size() ? paid.get(i) : this.lastSettled;
		}
	}
}
