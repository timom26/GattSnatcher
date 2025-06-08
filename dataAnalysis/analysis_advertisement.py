import sys
sys.path.append('./')  # Ensure root project dir is on path
from analysis_gatt import process_all_gatt_profiles, jaccard_similarity
FILEPATH_ROOT = './dataFiles/scanner/'
GATT_FILEPATH_ROOT = './dataFiles/questioner/'
FILEPATH_PROCESSED_EXT = '/processed/'
FILEPATH_OUTPUT_EXT = '/output/'
FOLDER_NAME = '/2025-05-18-15-00-00/'
FILEPATH = FILEPATH_ROOT + FILEPATH_PROCESSED_EXT + FOLDER_NAME
INVALID_JACCARD_SCORE = -2

import csv
import os
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.colors as mcolors
from datetime import datetime
import networkx as nx


CANVAS_WIDTH = 42 #inches?

# Visualization inclusion
INCLUDE_SINGLE_MAC = False
INCLUDE_PAIRED_MAC = True 

max_MACs_to_print_matrix_score = 15

# Optional: filter only specific MAC addresses. If empty, keep all.
MAC_FILTER = set([

])
MAC_FILTER = {mac.upper() for mac in MAC_FILTER}

VERY_HIGH_COST = 1e9
TOLERABLE_COST = 1e6


def plot_all_devices(df, summary, source_file=None, mac_chains=None, jaccard_scores=None):
    if summary.empty:
        print(f"Skipping plot for {source_file} – summary is empty.")
        return

    summary_sorted = summary.sort_values(by='first_seen').reset_index(drop=True)

    df = df.sort_values(by='timestamp_us')
    max_adv_len = df['adv_data_length'].max()
    min_height = 0.1
    max_height = 0.45

    rssi_min, rssi_max = df['rssi'].min(), df['rssi'].max()
    norm = mcolors.Normalize(vmin=rssi_min, vmax=rssi_max)
    cmap = plt.get_cmap('RdYlGn')

    df['color'] = [cmap(norm(r)) for r in df['rssi']]
    df['height'] = min_height + (df['adv_data_length'] / max_adv_len) * (max_height - min_height)

    # Include both chained groups and individual MACs, with switches
    all_macs = summary_sorted['mac_address'].tolist()
    mac_set_in_chains = set(mac for chain in mac_chains or [] for mac in chain)
    mac_chains = mac_chains or []
    # Build mac_list based on switches
    mac_list = []
    if INCLUDE_PAIRED_MAC:
        mac_list.extend(mac_chains)
    if INCLUDE_SINGLE_MAC:
        mac_list.extend([[mac] for mac in all_macs])
    yticks = [", ".join(group) for group in mac_list]

    # Frame color and Jaccard label logic
    frame_colors = []
    jaccard_labels = []
    for mac_group in mac_list:
        score = 0
        if jaccard_scores and len(mac_group) == 2:
            score = jaccard_scores.get((mac_group[0], mac_group[1]), 0)
        cmap = plt.get_cmap('RdYlGn')
        frame_colors.append(cmap(score))
        jaccard_labels.append(f"{int(score * 100)}%" if score else "")

    fig, ax = plt.subplots(figsize=(CANVAS_WIDTH, 0.6 * len(mac_list)))
    graph_start_offset = 500  # milliseconds, offset for label/marker positioning

    for i, mac_group in enumerate(mac_list):
        group_df = df[df['mac_address'].isin(mac_group)].copy()
        group_df = group_df.sort_values(by='timestamp_us')
        times_ms = (group_df['timestamp_us'] - df['timestamp_us'].min()) / 1000
        heights = group_df['height'].values
        colors = group_df['color'].values
        ax.vlines(times_ms, i - heights, i + heights, color=colors, linewidth=2)

        # Draw black line between MAC transitions within group
        for j in range(1, len(mac_group)):
            prev_mac = mac_group[j - 1]
            curr_mac = mac_group[j]
            if curr_mac in df['mac_address'].values:
                t = (df[df['mac_address'] == curr_mac]['timestamp_us'].min() - df['timestamp_us'].min()) / 1000
                ax.vlines(t, i - 0.5, i + 0.5, color='black', linewidth=2)

    # Y-axis labels (device groups only)
    display_labels = yticks
    ax.set_yticks(range(len(display_labels)))
    ax.set_yticklabels(display_labels, fontsize=14)

    # Similarity markers: colored ball + percentage or dashes
    sim_cmap = plt.get_cmap('RdYlGn')
    marker_x = -graph_start_offset * 0.8
    text_x = marker_x + graph_start_offset * 0.1
    for i, mac_group in enumerate(mac_list):
        if len(mac_group) == 2:
            key = (mac_group[0], mac_group[1])
            score = jaccard_scores.get(key, jaccard_scores.get((mac_group[1], mac_group[0]), np.nan))
            if score == INVALID_JACCARD_SCORE or np.isnan(score):
                color = 'grey'
                label = '   --'
            else:
                color = sim_cmap(score)
                label = f"   {int(score * 100)}%"
            ax.scatter(marker_x, i, s=100, color=color, zorder=5, clip_on=False)
            ax.text(text_x, i, label, va='center', fontsize=9, clip_on=False)

    ax.set_xlabel("Time (ms since beginning)")
    title = "BLE Advertisements by Device (sorted by first seen)"
    # if source_file:
        # title += f"\n{source_file}"
    ax.set_title(title)
    graph_start_offset = 500  # milliseconds, adjust as needed
    ax.set_xlim(-graph_start_offset,
                (df['timestamp_us'].max() - df['timestamp_us'].min()) / 1000)
    ax.grid(True, axis='x', linestyle='--', alpha=0.3)

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    safe_filename = source_file.replace('/', '_').replace('\\', '_') if source_file else "unknown"
    output_path1 = FILEPATH_ROOT + FILEPATH_OUTPUT_EXT + FOLDER_NAME
    os.makedirs(output_path1, exist_ok=True)
    output_file_full_path = output_path1 + f"/advertisement_timeline_{timestamp}_{safe_filename}.png"
    plt.subplots_adjust(left=0.4) 
    plt.tight_layout()
    plt.savefig(output_file_full_path, dpi=300)
    plt.close()
    print(f"Saved plot to {output_file_full_path}")

# Group MAC addresses likely belonging to the same device
def cost_function(summary, df, mac1, mac2, rssi_margin=25, max_interval_diff=1.0, max_time_gap_multiplier=2.0):
    mac1_df = df[df['mac_address'] == mac1].sort_values(by='timestamp_us').tail(5)
    mac2_df = df[df['mac_address'] == mac2].sort_values(by='timestamp_us').head(5)

    # Insert check for overlapping or out-of-order MAC timing
    if mac1_df['timestamp_us'].iloc[-1] >= mac2_df['timestamp_us'].iloc[0]:
        return VERY_HIGH_COST

    if len(mac1_df) < 5 or len(mac2_df) < 5:
        return VERY_HIGH_COST
    x1 = mac1_df['timestamp_us'].values
    y1 = mac1_df['rssi'].values
    x2 = mac2_df['timestamp_us'].values
    y2 = mac2_df['rssi'].values

    #normalization
    x1_norm = (x1 - x1[0]) / 1000.0
    x2_norm = (x2 - x1[0]) / 1000.0

    try:
        coeffs = np.polyfit(x1_norm, y1, 1)
        y2_pred = np.polyval(coeffs, x2_norm)
        rssi_loss = np.mean((y2 - y2_pred) ** 2)
    except np.RankWarning:
        rssi_loss += VERY_HIGH_COST

    # Advertisement size loss
    mac1_sizes = mac1_df['adv_data_length'].values
    mac2_sizes = mac2_df['adv_data_length'].values

    adv_size_loss = 0
    unique1 = np.unique(mac1_sizes)
    unique2 = np.unique(mac2_sizes)

    # if only one size of last five advertisement payloads
    #and the same size is of the other MAC address
    if (len(unique1) == 1) and (len(unique2) == 1) and (unique2[0] == unique1[0]): #
        adv_size_loss = 0

    elif len(unique2) == 5:
        adv_size_loss = VERY_HIGH_COST
    elif any(size in mac1_sizes for size in mac2_sizes):
        adv_size_loss = 10000  # It was at least able to produce one advertisement of the same length.
    else:
        adv_size_loss = VERY_HIGH_COST
        # adv_size_loss += 5000 + 500 * len(unique2)

    # Advertising interval loss
    row1 = summary[summary['mac_address'] == mac1].iloc[0]
    row2 = summary[summary['mac_address'] == mac2].iloc[0]
    interval1 = row1['estimated_true_interval_ms']
    interval2 = row2['estimated_true_interval_ms']

    interval_diff = abs(interval1 - interval2)
    interval_avg = (interval1 + interval2) / 2.0

    interval_loss = (interval_diff ** 2)

    # Timing regularity and gap
    combined_timestamps = np.concatenate([mac1_df['timestamp_us'].values, mac2_df['timestamp_us'].values])
    diffs = np.diff(combined_timestamps) / 1000.0  # to ms
    regular_diffs = np.all(np.abs(diffs - interval1) < 10.0)

    last_time_mac1 = mac1_df['timestamp_us'].iloc[-1]
    first_time_mac2 = mac2_df['timestamp_us'].iloc[0]
    gap_ms = (first_time_mac2 - last_time_mac1) / 1000.0

    if regular_diffs:
        if gap_ms <= interval1 * 3:
            timing_loss = 0
        elif gap_ms <= interval1 * 10:
            timing_loss = ((gap_ms - interval1) ** 2)
        else:
            timing_loss = (((gap_ms - interval1 * 10) / 1000) ** 2) + 10000
    else:
        timing_loss = 1000 + ((gap_ms / 1000) ** 2)


    total_loss = rssi_loss + adv_size_loss + interval_loss + timing_loss
    # print(f"Cost {mac1} → {mac2}: "
    #       f"RSSI={rssi_loss:.1f}, ADV_SIZE={adv_size_loss:.1f}, "
    #       f"INTERVAL={interval_loss:.1f}, TIMING={timing_loss:.1f}, "
    #       f"TOTAL={rssi_loss + adv_size_loss + interval_loss + timing_loss:.1f}")
    return total_loss

def resolve_mac_pairings_via_graph(accepted_pairs):
    cost_lookup = {(a, b): cost for a, b, cost in accepted_pairs}

    # Build bipartite graph
    graph = nx.Graph()
    for a, b, cost in accepted_pairs:
        graph.add_edge(f"L_{a}", f"R_{b}", weight=-cost)#inverted value as we look for minimum cost pair

    # Compute minimum cost pairwise matching while keeping maximum possible number of pairs
    matching = nx.algorithms.matching.max_weight_matching(graph, maxcardinality=True)
    resolved_pairs = []
    for u, v in matching:
        if u.startswith("L_") and v.startswith("R_"):
            a = u[2:]
            b = v[2:]
        else:
            a = v[2:]
            b = u[2:]
        resolved_pairs.append((a, b, cost_lookup.get((a, b), cost_lookup.get((b, a), 0))))

    print("\nResolved MAC pairs after matching:")
    for a, b, cost in resolved_pairs:
        print(f"  {a} → {b} (cost: {cost:.2f})")

    return resolved_pairs

def find_best_mac_pair(summary, df, max_cost=TOLERABLE_COST):
    accepted_pairs = []
    mac_addresses = summary['mac_address'].tolist()
    verbose_matrix_output = len(mac_addresses) <= max_MACs_to_print_matrix_score

    total = len(mac_addresses)
    step = max(1, total // 4)
    ranges = [(i, min(i + step, total)) for i in range(0, total, step)]

    index_map = {mac: idx for idx, mac in enumerate(mac_addresses)}

    for r_start, r_end in ranges:
        if verbose_matrix_output:
            print(f"\nIndexes {r_start} to {r_end - 1}")
            header = ["        "] + [f"{i:>8}" for i in range(r_start, r_end)]
            print("".join(header))
        for i in range(total):
            row = [f"{i:>8}"]
            for j in range(r_start, r_end):
                if i >= j:
                    row.append("     --- ")
                else:
                    cost = cost_function(summary, df, mac_addresses[i], mac_addresses[j])
                    if cost < max_cost:
                        accepted_pairs.append((mac_addresses[i], mac_addresses[j], cost))
                    if verbose_matrix_output:
                        if cost >= VERY_HIGH_COST:
                            row.append("     Inf ")
                        elif cost > 0:
                            row.append(f"{cost:9.2f}")
                        else:
                            row.append("    0.00 ")
            if verbose_matrix_output:
                print("".join(row))

    if accepted_pairs:
        print("\nAccepted MAC pairs:")
        for a, b, cost in accepted_pairs:
            print(f"  {a} → {b} (cost: {cost:.2f})")
    else:
        print("NO best pair found!!")
        return []

    return resolve_mac_pairings_via_graph(accepted_pairs)


def estimate_advertising_interval(df, mac_address, jitter_max_ms=10.0):
    device_df = df[df['mac_address'] == mac_address].copy()
    device_df = device_df.sort_values(by='timestamp_us')

    # Calculate deltas in milliseconds
    deltas_ms = device_df['timestamp_us'].diff().dropna() / 1000.0

    # Estimate T_adv as the minimum observed delta
    estimated_interval_ms = deltas_ms.min()
    spread = deltas_ms.max() - deltas_ms.min()
    num_observations = len(deltas_ms)

    # Confidence: based on how well the jitter spread matches the expected max
    spread_ratio = min(1.0, spread / jitter_max_ms)
    completeness_factor = min(1.0, num_observations / 20.0)
    confidence = (1.0 - abs(1.0 - spread_ratio)) * completeness_factor * 100.0

    return estimated_interval_ms, confidence


def round_down_to_valid_ble_interval(interval_ms, step_ms=0.625):

    return (interval_ms // step_ms) * step_ms

def plot_advertisements_for_mac(df, mac_address, ax=None):
    device_df = df[df['mac_address'] == mac_address].copy()
    device_df = device_df.sort_values(by='timestamp_us')
    device_df['time_ms'] = (device_df['timestamp_us'] - device_df['timestamp_us'].min()) / 1000
    device_df['delta_ms'] = device_df['time_ms'].diff()

    if ax is None:
        fig, ax = plt.subplots(figsize=(12, 2))

    norm = mcolors.Normalize(vmin=df['rssi'].min(), vmax=df['rssi'].max())
    cmap = plt.get_cmap('coolwarm')

    for i in range(len(device_df)):
        t = device_df.iloc[i]['time_ms']
        rssi = device_df.iloc[i]['rssi']
        color = cmap(norm(rssi))
        ax.plot([t, t], [0, 1], color=color, linewidth=3)
        if i > 0:
            delta = device_df.iloc[i]['delta_ms']
            prev_t = device_df.iloc[i-1]['time_ms']
            ax.text((t + prev_t) / 2, 1.05, f"{delta:.1f} ms", ha='center', fontsize=8)

    ax.set_title(f"Advertisements for {mac_address}")
    ax.set_xlabel("Time (ms)")
    ax.set_yticks([])
    ax.set_xlim(device_df['time_ms'].min() - 10, device_df['time_ms'].max() + 10)

    return ax

def remove_interval_outliers(df, fallback_threshold_ms=1000):
    df = df.sort_values(by=['mac_address', 'timestamp_us'])
    df['delta_us'] = df.groupby('mac_address')['timestamp_us'].diff()

    counts = df.groupby('mac_address')['delta_us'].count()
    large_sample_macs = counts[counts >= 100].index
    small_sample_macs = counts[counts < 100].index

    mask = pd.Series(True, index=df.index)

    for mac in large_sample_macs:
        mac_df = df[df['mac_address'] == mac]
        deltas = mac_df['delta_us'].dropna()
        mean = deltas.mean()
        std = deltas.std()
        lower_cutoff = mean - 2 * std
        upper_cutoff = mean + 2 * std
        mac_mask = (
            (df['mac_address'] != mac) |
            df['delta_us'].isna() |
            ((df['delta_us'] >= lower_cutoff) & (df['delta_us'] <= upper_cutoff))
        )
        mask &= mac_mask

    for mac in small_sample_macs:
        mac_mask = (df['mac_address'] != mac) | df['delta_us'].isna() | (df['delta_us'] < fallback_threshold_ms * 1000)
        mask &= mac_mask

    df.loc[~mask, 'delta_us'] = np.nan
    return df.drop(columns=['delta_us'])


def mainTask():
    import os
    # input_files = [f for f in os.listdir(FILEPATH) if f.endswith('.csv')]
    input_files = ["glued_advertisements.csv"]
    for filename in input_files:
        print("currently processing", FILEPATH,filename)
        input_path = os.path.join(FILEPATH, filename)
        df = pd.read_csv(input_path)
        if MAC_FILTER:
            df = df[df['mac_address'].isin(MAC_FILTER)]
        df['timestamp_us'] = df['timestamp_us'].astype(int)
        df['rssi'] = df['rssi'].astype(int)

        grouped = df.groupby('mac_address')
        summary = grouped.agg(
            count=('timestamp_us', 'count'),
            rssi_min=('rssi', 'min'),
            rssi_avg=('rssi', 'mean'),
            rssi_max=('rssi', 'max'),
            first_seen=('timestamp_us', 'min'),
            last_seen=('timestamp_us', 'max')
        ).reset_index()

        summary['duration_s'] = (summary['last_seen'] - summary['first_seen']) / 1_000_000
        summary = summary[summary['duration_s'] >= 3]
        summary = summary[summary['count'] >= 3]

        intervals = []
        confidences = []

        for mac in summary['mac_address']:
            interval, conf = estimate_advertising_interval(df, mac)
            intervals.append(interval)
            confidences.append(conf)

        summary['estimated_interval_ms'] = intervals
        summary['estimated_true_interval_ms'] = summary['estimated_interval_ms'].apply(round_down_to_valid_ble_interval)
        summary['confidence'] = confidences
        summary = summary.sort_values(by='first_seen').reset_index(drop=True)

        renamed = summary.rename(columns={
            'rssi_min': 'LRS',
            'rssi_avg': 'MRS',
            'rssi_max': 'HRS',
            'estimated_interval_ms': 'est_int_ms',
            'estimated_true_interval_ms': 'est_t_int_ms',
            'confidence': 'CI'
        })
        print(renamed)

        mac_pairs = find_best_mac_pair(summary, df)
        mac_chains = [[a, b] for a, b, _ in mac_pairs]

        # Load all GATT profiles for Jaccard similarity
        gatt_dir = os.path.join("./dataFiles/questioner/unprocessed/", FOLDER_NAME.strip('/'))
        gatt_path = os.path.join(gatt_dir, "glued_file.bin")
        print("GATT PATH is ",gatt_path)
        gatt_macs, gatt_profile_list = process_all_gatt_profiles(gatt_path)
        gatt_macs = [mac.upper() for mac in gatt_macs]
        print("GATT MACS ",gatt_macs)
        print("GATT profiles", gatt_profile_list)
        # Compute Jaccard similarity by matching mac indices
        jaccard_scores = {}
        for a, b, _ in mac_pairs:
            if a in gatt_macs and b in gatt_macs:
                idx_a = gatt_macs.index(a)
                idx_b = gatt_macs.index(b)
                set_a = gatt_profile_list[idx_a]
                set_b = gatt_profile_list[idx_b]
                score = jaccard_similarity(set_a, set_b)
                jaccard_scores[(a, b)] = score
            else:
                missing = []
                if a not in gatt_macs:
                    missing.append(a)
                if b not in gatt_macs:
                    missing.append(b)
                print(f"Warning: missing GATT profile for {', '.join(missing)}")
                jaccard_scores[(a, b)] = INVALID_JACCARD_SCORE
        print("jaccard scores are",jaccard_scores)
        for a, b, t in mac_pairs:
            print(f"{a} → {b} (gap: {t:.1f} ms)")

            a_df = df[df['mac_address'] == a].sort_values(by='timestamp_us')
            b_df = df[df['mac_address'] == b].sort_values(by='timestamp_us')

            a_last_two = a_df.tail(2)
            b_first_two = b_df.head(2)

            print(f"  [{a}] Estimated interval: {summary[summary['mac_address'] == a]['estimated_true_interval_ms'].values[0]} ms")
            print(f"  [{b}] Estimated interval: {summary[summary['mac_address'] == b]['estimated_true_interval_ms'].values[0]} ms")
            print(f"  [{a}] Last RSSIs: {a_last_two['rssi'].tolist()} @ {a_last_two['timestamp_us'].tolist()}")
            print(f"  [{b}] First RSSIs: {b_first_two['rssi'].tolist()} @ {b_first_two['timestamp_us'].tolist()}")
            if jaccard_scores:
                score = jaccard_scores.get((a, b)) or jaccard_scores.get((b, a))
                if score is not None:
                    print(f"  [{a} ↔ {b}] GATT Jaccard similarity: {score:.2%}")
                else:
                    print(f"  [{a} ↔ {b}] GATT Jaccard similarity:")
            else:
                print("no jaccard scores")
            print()
        if not df.empty:
            plot_all_devices(df, summary, filename, mac_chains, jaccard_scores=jaccard_scores)
        else:
            print(f"Skipping plot for {filename} – dataframe is empty.")
def print_data_with_timestamps(df):
    df_sorted = df.sort_values(by=['mac_address', 'timestamp_us']).copy()
    df_sorted['time_since_last_seen'] = df_sorted.groupby('mac_address')['timestamp_us'].diff().fillna(0) / 1000.0  # in ms
    print("\nData with time_since_last_seen (in ms):")
    print(df_sorted[['timestamp_us', 'mac_address', 'rssi', 'time_since_last_seen']])

if __name__ == "__main__":
    print("start")
    mainTask()
    print("end")
