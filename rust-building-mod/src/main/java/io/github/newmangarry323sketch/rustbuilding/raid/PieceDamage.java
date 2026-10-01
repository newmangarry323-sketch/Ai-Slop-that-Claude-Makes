package io.github.newmangarry323sketch.rustbuilding.raid;

import java.util.ArrayList;
import java.util.List;

import com.mojang.serialization.Codec;
import com.mojang.serialization.codecs.RecordCodecBuilder;
import it.unimi.dsi.fastutil.longs.Long2IntMap;
import it.unimi.dsi.fastutil.longs.Long2IntOpenHashMap;

import net.minecraft.core.BlockPos;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.level.saveddata.SavedData;
import net.minecraft.world.level.saveddata.SavedDataType;

import io.github.newmangarry323sketch.rustbuilding.RustBuilding;

/**
 * Damage taken by pieces and doors, per dimension, keyed by each piece's anchor block. Only damaged
 * pieces have an entry, so an undamaged base costs nothing to store.
 */
public final class PieceDamage extends SavedData {
	private record Entry(long pos, int damage) {
		static final Codec<Entry> CODEC = RecordCodecBuilder.create(instance -> instance.group(
				Codec.LONG.fieldOf("pos").forGetter(Entry::pos),
				Codec.INT.fieldOf("damage").forGetter(Entry::damage)
		).apply(instance, Entry::new));
	}

	private static final Codec<PieceDamage> CODEC = Entry.CODEC.listOf().xmap(PieceDamage::new, PieceDamage::entries);

	public static final SavedDataType<PieceDamage> TYPE = new SavedDataType<>(
			RustBuilding.id("piece_damage"),
			PieceDamage::new,
			CODEC,
			null
	);

	private final Long2IntOpenHashMap damage = new Long2IntOpenHashMap();

	public PieceDamage() {
	}

	private PieceDamage(List<Entry> entries) {
		for (Entry entry : entries) {
			this.damage.put(entry.pos(), entry.damage());
		}
	}

	private List<Entry> entries() {
		List<Entry> entries = new ArrayList<>(this.damage.size());

		for (Long2IntMap.Entry entry : this.damage.long2IntEntrySet()) {
			entries.add(new Entry(entry.getLongKey(), entry.getIntValue()));
		}

		return entries;
	}

	public static PieceDamage get(ServerLevel level) {
		return level.getDataStorage().computeIfAbsent(TYPE);
	}

	public int get(BlockPos anchor) {
		return this.damage.get(anchor.asLong());
	}

	/** Adds damage and returns the new total. */
	public int add(BlockPos anchor, int amount) {
		long key = anchor.asLong();
		int total = this.damage.get(key) + amount;
		this.damage.put(key, total);
		this.setDirty();
		return total;
	}

	public void clear(BlockPos anchor) {
		if (this.damage.containsKey(anchor.asLong())) {
			this.damage.remove(anchor.asLong());
			this.setDirty();
		}
	}
}
