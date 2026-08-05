from skyfield.api import load, wgs84
from datetime import datetime, timedelta
import pytz
import csv
from tqdm import tqdm 
import os

OUT_DIR = "output"

# === 1. 載入最新 TLE ===
stations = load.tle_file("https://celestrak.org/NORAD/elements/gp.php?GROUP=starlink&FORMAT=tle")
total_sats = len(stations)
print(f"✅ 成功下載 {total_sats} 組 TLE 資料。")

# === 2. 定義觀測點（新竹） ===
hsinchu = wgs84.latlon(24.8, 120.97, 0)

# === 3. 建立時間區間（未來 2 小時） ===
ts = load.timescale()
tz = pytz.timezone("Asia/Taipei")
t_start = ts.now()  # 改用 skyfield 的
t_end = ts.from_datetime(t_start.utc_datetime() + timedelta(hours=2))
start_time_dt = t_start.astimezone(tz) # 用於後續計算

# === 4. 第一階段：快速篩選 (Filter) ===
#    使用 find_events() 演算法，快速篩選 8521 顆衛星...
print(f"\n使用 find_events() 演算法，快速篩選 {total_sats} 顆衛星...")
visible_satellites = []
# 使用 tqdm 顯示進度條
for sat in tqdm(stations, desc="-> 處理進度", unit=" 顆衛星"):
    # 尋找衛星仰角 > 0 (地平線以上) 的事件
    t, events = sat.find_events(hsinchu, t_start, t_end, altitude_degrees=20.0)
    # 0=升起, 1=最高點, 2=落下
    # 如果 0 (升起) 事件存在，代表它會經過
    if 0 in events:
        visible_satellites.append(sat)

visible_count = len(visible_satellites)
print(f"\n✅ 計算完成！在未來 2 小時內，共找到 {visible_count} 顆衛星會經過新竹上空。")


# === 5. 第二階段：計算詳細位置 ===
print(f"\n正在計算這 {visible_count} 顆衛星的每分鐘詳細軌跡...")
visible_log = []

# 建立每分鐘的時間點
minutes = [start_time_dt + timedelta(minutes=i) for i in range(0, 120, 1)]  # 每分鐘
times_utc = ts.from_datetimes(minutes) # 轉換為 skyfield 的時間物件

# 只針對可見的衛星進行詳細計算
for sat in tqdm(visible_satellites, desc="-> 計算詳細軌跡", unit=" 顆衛星"):
    sat_name = sat.name
    difference = sat - hsinchu
    
    # 這裡我們一次取得 仰角(elevation)、方位角(azimuth)、斜距(distance)
    elevations, azimuths, distances = difference.at(times_utc).altaz()
    
    # 取得衛星在地心座標系統 (ECEF) 的位置
    geocentric = sat.at(times_utc)
    # NOTE: geocentric.position.km returns GCRS (Geocentric Celestial Reference System)
    # coordinates — an Earth-centred INERTIAL frame aligned with J2000 equinox.
    # This is NOT the same as ITRF/ECEF (Earth-fixed).  The columns are labelled
    # x_ecef_km/y_ecef_km/z_ecef_km in the CSV for historical reasons, but the values
    # are GCRS.  The C++ simulation uses them only for Doppler direction estimation
    # (velocity from Δposition/Δt), which is valid because the satellite velocity in GCRS
    # is the same as in ECEF for this purpose.  The slant-range distance used for path
    # loss (distance_km column) is correctly obtained from Skyfield's altaz() below.
    position = geocentric.position.km  # GCRS frame (stored as x_ecef_km in CSV)
    x_ecef = position[0]
    y_ecef = position[1]
    z_ecef = position[2]
    
    subpoints = geocentric.subpoint()
    latitudes = subpoints.latitude.degrees
    longitudes = subpoints.longitude.degrees
    heights = subpoints.elevation.km

    for i, el in enumerate(elevations.degrees):
        if el > 0:  # 衛星在地平線以上
            # Skip decommissioned / re-entering satellites.
            # Operational Starlink LEO altitude is 530-580 km.  A satellite below 300 km
            # is undergoing atmospheric re-entry; including it inflates SNR by 10+ dB
            # due to its artificially short slant range.
            if heights[i] < 300.0:
                continue
            t_str = minutes[i].strftime("%Y-%m-%d %H:%M:%S")
            visible_log.append({
                "time": t_str,
                "satellite": sat_name,
                "elevation_deg": round(el, 2),        # elevation_deg (仰角)： 您要抬頭多高
                "azimuth_deg": round(azimuths.degrees[i], 2), # azimuth_deg (方位角)： 您要面向哪個羅盤方向？
                "distance_km": round(distances.km[i], 2), # distance_km (斜距)： 衛星離您有多遠？
                "lat_subpoint": round(latitudes[i], 2),  # lat_subpoint (星下點緯度): 衛星飛在地球上哪個緯度的上空？
                "lon_subpoint": round(longitudes[i], 2),  # lon_subpoint (星下點經度)：衛星飛在地球上哪個經度的上空？
                "height_km": round(heights[i], 2), # height_km (衛星高度)：距離地表有多遠
                "x_ecef_km": round(x_ecef[i], 2),  # X 地心座標 (ECEF)
                "y_ecef_km": round(y_ecef[i], 2),  # Y 地心座標 (ECEF)
                "z_ecef_km": round(z_ecef[i], 2)   # Z 地心座標 (ECEF)
            })

# === 6. 顯示結果 ===

if not visible_log:
    print("❌ 沒有衛星經過新竹上空")
else:
    # for entry in visible_log[:5]:
    #     # 修正：更新 print 內容以包含新資訊
    #     print(f"[{entry['time']}] {entry['satellite']}: "
    #           f"仰角(el)={entry['elevation_deg']}° "
    #           f"方位(az)={entry['azimuth_deg']}° "
    #           f"斜距(dist)={entry['distance_km']}km "
    #           f"星下點=({entry['lat_subpoint']}, {entry['lon_subpoint']}) "
    #           f"高度={entry['height_km']}km")

    print(f"\n✅ 共 {len(visible_log)} 筆可見衛星資料點（2小時內）")

    # 按時間排序
    visible_log.sort(key=lambda x: x['time'])

    if not os.path.exists(OUT_DIR):
        os.makedirs(OUT_DIR)

    output_file = os.path.join(OUT_DIR, "visible_satellites_hsinchu.csv")

    with open(output_file, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=visible_log[0].keys())
        writer.writeheader()
        writer.writerows(visible_log)

    print(f"已匯出結果到 {output_file}")