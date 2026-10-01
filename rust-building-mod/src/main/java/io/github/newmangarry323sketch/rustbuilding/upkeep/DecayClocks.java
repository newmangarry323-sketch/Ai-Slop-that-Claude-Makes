package io.github.newmangarry323sketch.rustbuilding.upkeep;

import java.util.ArrayList;
import java.util.List;

import com.mojang.serialization.Codec;
import com.mojang.serialization.codecs.RecordCodecBuilder;
import it.unimi.dsi.fastutil.longs.Long2LongMap;
import it.unimi.dsi.fastutil.longs.Long2LongOpenHashMap;

import net.minecraft.core.BlockPos;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.level.saveddata.SavedData;
import net.minecraft.world.level.saveddata.SavedDataType;

import io.github.newmangarry323sketch.rustbuilding.RustBuilding;

/**
 * For each decaying piece, keyed by its anchor block, the game time up to which its decay has been
 * counted. Kept per dimension and saved, so a base left unloaded for a while catches up on its decay
 * when someone comes back. Pieces that a cupboard pays for have no entry.
 */
public final class DecayClocks extends SavedData {
	/** Returned for a piece that has no entry. */
	public static final long NONE = Long.MIN_VALUE;

	private record Entry(long pos, long since) {
		static final Codec<Entry> CODEC = RecordCodecBuilder.create(instance -> instance.group(
				Codec.LONG.fieldOf("pos").forGetter(Entry::pos),
				Codec.LONG.fieldOf("since").forGetter(Entry::since)
		).apply(instance, Entry::new));
	}

	private static final Codec<DecayClocks> CODEC = Entry.CODEC.listOf().xmap(DecayClocks::new, DecayClocks::entries);

	public static final SavedDataType<DecayClocks> TYPE = new SavedDataType<>(
			RustBuilding.id("decay_clocks"),
			DecayClocks::new,
			CODEC,
			null
	);

	private final Long2LongOpenHashMap clocks = new Long2LongOpenHashMap();

	public DecayClocks() {
		this.clocks.defaultReturnValue(NONE);
	}

	private DecayClocks(List<Entry> entries) {
		this();

		for (Entry entry : entries) {
			this.clocks.put(entry.pos(), entry.since());
		}
	}

	private List<Entry> entries() {
		List<Entry> entries = new ArrayList<>(this.clocks.size());

		for (Long2LongMap.Entry entry : this.clocks.long2LongEntrySet()) {
			entries.add(new Entry(entry.getLongKey(), entry.getLongValue()));
		}

		return entries;
	}

	public static DecayClocks get(ServerLevel level) {
		return level.getDataStorage().computeIfAbsent(TYPE);
	}

	public long get(BlockPos anchor) {
		return this.clocks.get(anchor.asLong());
	}

	public void set(BlockPos anchor, long since) {
		if (this.clocks.put(anchor.asLong(), since) != since) {
			this.setDirty();
		}
	}

	public void clear(BlockPos anchor) {
		if (this.clocks.containsKey(anchor.asLong())) {
			this.clocks.remove(anchor.asLong());
			this.setDirty();
		}
	}
}
