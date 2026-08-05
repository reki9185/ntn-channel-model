
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/propagation-module.h"
#include "ns3/spectrum-module.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <cmath>
#include <random>
#include <filesystem>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Sgp4NtnFixedDistanceExample");

// Structure to hold satellite trajectory data
struct TrajectoryPoint
{
    double time;          // Seconds since start
    double x_ecef;        // ECEF X in meters
    double y_ecef;        // ECEF Y in meters
    double z_ecef;        // ECEF Z in meters
    double elevation;     // Elevation angle in degrees
    double csv_distance;  // Distance from CSV (for verification)
};

// Global variables
std::vector<TrajectoryPoint> g_trajectory;
std::ofstream g_channelLog;
std::ofstream g_snrLog;
std::ofstream g_dopplerLog;
std::ofstream g_fullLog;  
Ptr<RateErrorModel> g_uplinkErrorModel;
Ptr<RateErrorModel> g_downlinkErrorModel;
Ptr<ThreeGppPropagationLossModel> g_lossModel;
Ptr<PointToPointChannel> g_p2pChannel;  // For dynamic delay updates
Ptr<FlowMonitor> g_flowMonitor;  // Global FlowMonitor for PDR tracking
Ptr<Ipv4FlowClassifier> g_flowClassifier;  // Global FlowClassifier

// PDR tracking variables
uint64_t g_prevTxPackets = 0;
uint64_t g_prevRxPackets = 0;

// Ground station ECEF position (calculated once)
double g_groundX, g_groundY, g_groundZ;

// Speed of light constant
const double SPEED_OF_LIGHT = 299792458.0;  // m/s

// Global parameters
double g_frequency;
double g_txPower;
double g_noiseFigure;
double g_bandwidth;
double g_fecCodingGain;
double g_txAntennaGain;
double g_rxGain;
uint32_t g_packetSize;
std::string scenario = "Rural";
uint32_t g_randomSeed = 45;         // Random seed for reproducibility

std::map<int, double> g_shadowFadingStd_Rural = {
    {10, 1.9}, {20, 1.6}, {30, 1.9}, {40, 2.3}, {50, 2.7},
    {60, 3.1}, {70, 3.0}, {80, 3.6}, {90, 0.4}
};
// Suburban — shares SFCL_SuburbanRural table with Rural (Ka_LOS_sigF column)
std::map<int, double> g_shadowFadingStd_Suburban = {
    {10, 1.9}, {20, 1.6}, {30, 1.9}, {40, 2.3}, {50, 2.7},
    {60, 3.1}, {70, 3.0}, {80, 3.6}, {90, 0.4}
};
// Urban — SFCL_Urban[elev][Ka_LOS_sigF]: constant 4.0 dB across all elevations
std::map<int, double> g_shadowFadingStd_Urban = {
    {10, 4.0}, {20, 4.0}, {30, 4.0}, {40, 4.0}, {50, 4.0},
    {60, 4.0}, {70, 4.0}, {80, 4.0}, {90, 4.0}
};
// Dense Urban — SFCL_DenseUrban[elev][Ka_NLOS_sigF]: elevation-dependent
std::map<int, double> g_shadowFadingStd_DenseUrban = {
    {10, 2.9}, {20, 2.4}, {30, 2.7}, {40, 2.4}, {50, 2.4},
    {60, 2.7}, {70, 2.6}, {80, 2.8}, {90, 0.6}
};
// Keep legacy alias pointing to Rural (used by UpdateSatelliteState reference channel)
// const std::map<int, double>& g_shadowFadingStd = g_shadowFadingStd_Rural;

// 3GPP TR 38.811 Table 6.6.6.2.1-1: Tropospheric scintillation loss (dB) at 20 GHz
// (physics-based, same for all environments)
std::map<int, double> g_troposphericScintillation = {
    {10, 1.08}, {20, 0.48}, {30, 0.30}, {40, 0.22}, {50, 0.17},
    {60, 0.13}, {70, 0.12}, {80, 0.12}, {90, 0.12}
};

// 3GPP TR 38.811 Table 6.6.2-1: Shadow fading std dev σ_SF (dB), Ka-band LOS
// Source: SFCL_SuburbanRural[elev][Ka_NLOS_clutter] in three-gpp-propagation-loss-model.cc
// Rural — SFCL_SuburbanRural, Ka_NLOS_clutter column
// Dense Urban — SFCL_DenseUrban[elev][Ka_NLOS_clutter]
std::map<int, double> g_clutterLoss_DenseUrban_NLOS = {
    {10, 44.3}, {20, 39.9}, {30, 37.5}, {40, 35.8}, {50, 34.6},
    {60, 33.8}, {70, 33.3}, {80, 33.0}, {90, 32.9}
};
// Urban — SFCL_Urban[elev][Ka_NLOS_CL]
std::map<int, double> g_clutterLoss_Urban_NLOS = {
    {10, 44.3}, {20, 39.9}, {30, 37.5}, {40, 35.8}, {50, 34.6},
    {60, 33.8}, {70, 33.3}, {80, 33.0}, {90, 32.9}
};
// Rural — SFCL_SuburbanRural[elev][Ka_NLOS_CL]
std::map<int, double> g_clutterLoss_Rural_NLOS = {
    {10, 29.5}, {20, 24.6}, {30, 21.9}, {40, 20.0}, {50, 18.7},
    {60, 17.8}, {70, 17.2}, {80, 16.9}, {90, 16.8}
};
// Suburban — shares SFCL_SuburbanRural table with Rural (Ka_NLOS_CL column)
std::map<int, double> g_clutterLoss_Suburban_NLOS = {
    {10, 29.5}, {20, 24.6}, {30, 21.9}, {40, 20.0}, {50, 18.7},
    {60, 17.8}, {70, 17.2}, {80, 16.9}, {90, 16.8}
};

// Random generator for shadow fading and LOS state
std::map<std::string, bool> losStateCache;

// 3GPP TR 38.811 Table 6.6.2-1: Shadow fading std dev σ_SF (dB), Ka-band LOS
// Source: SFCL_SuburbanRural[elev][Ka_NLOS_sigF] in three-gpp-propagation-loss-model.cc
// Rural — SFCL_SuburbanRural, Ka_NLOS_sigF column
// Dense Urban — SFCL_DenseUrban[elev][Ka_NLOS_sigF]
std::map<int, double> g_shadowFadingStd_DenseUrban_NLOS = {
    {10, 17.1}, {20, 17.1}, {30, 15.6}, {40, 14.6}, {50, 14.2},
    {60, 12.6}, {70, 12.1}, {80, 12.3}, {90, 12.3}
};
// Urban — SFCL_Urban[elev][Ka_NLOS_sigF]
std::map<int, double> g_shadowFadingStd_Urban_NLOS = {
    {10, 6.0}, {20, 6.0}, {30, 6.0}, {40, 6.0}, {50, 6.0},
    {60, 6.0}, {70, 6.0}, {80, 6.0}, {90, 6.0}
};
// Rural — SFCL_Urban[elev][Ka_NLOS_sigF]
std::map<int, double> g_shadowFadingStd_Rural_NLOS = {
    {10, 10.7}, {20, 10.0}, {30, 11.2}, {40, 11.6}, {50, 11.8},
    {60, 10.8}, {70, 10.8}, {80, 10.8}, {90, 10.8}
};
// Suburban — shares SFCL_SuburbanRural table with Rural (Ka_NLOS_sigF column)
std::map<int, double> g_shadowFadingStd_Suburban_NLOS = {
    {10, 10.7}, {20, 10.0}, {30, 11.2}, {40, 11.6}, {50, 11.8},
    {60, 10.8}, {70, 10.8}, {80, 10.8}, {90, 10.8}
};

// Returns shadow fading table for the given scenario string (3GPP TR 38.811)
// Maps to SFCL_DenseUrban / SFCL_Urban / SFCL_SuburbanRural in three-gpp-propagation-loss-model.cc
const std::map<int, double>& GetShadowFadingTable(const std::string& scenario)
{
    // Case-insensitive substring match — check "dense" before plain "urban"
    std::string s = scenario;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    if (s.find("dense") != std::string::npos)
        return g_shadowFadingStd_DenseUrban;
    if (s.find("urban") != std::string::npos && s.find("suburban") == std::string::npos)
        return g_shadowFadingStd_Urban;
    if (s.find("suburban") != std::string::npos)
        return g_shadowFadingStd_Suburban;
    return g_shadowFadingStd_Rural;  // default: Rural
}

// Returns NLOS shadow fading table for the given scenario string (3GPP TR 38.811)
const std::map<int, double>& GetShadowFadingTable_NLOS(const std::string& scenario)
{
    // Case-insensitive substring match — check "dense" before plain "urban"
    std::string s = scenario;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    if (s.find("dense") != std::string::npos)
        return g_shadowFadingStd_DenseUrban_NLOS;
    if (s.find("urban") != std::string::npos && s.find("suburban") == std::string::npos)
        return g_shadowFadingStd_Urban_NLOS;
    if (s.find("suburban") != std::string::npos)
        return g_shadowFadingStd_Suburban_NLOS;
    return g_shadowFadingStd_Rural_NLOS;  // default: Rural
}


// Per-scenario clutter loss (dB) — 3GPP TR 38.811 Section 6.6.5
// LOS: no clutter loss for any scenario (confirmed in ThreeGppNTN*::GetLossLos in ns-3).
// NLOS clutter loss is elevation-dependent from SFCL tables and is NOT applied here
// because this simulation assumes LOS-only satellite links.
const std::map<int, double>& GetClutterLossTable(const std::string& scenario)
{
    std::string s = scenario;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    if (s.find("dense") != std::string::npos)
        return g_clutterLoss_DenseUrban_NLOS;
    if (s.find("urban") != std::string::npos && s.find("suburban") == std::string::npos)
        return g_clutterLoss_Urban_NLOS;
    if (s.find("suburban") != std::string::npos)
        return g_clutterLoss_Suburban_NLOS;
    return g_clutterLoss_Rural_NLOS;  // default: Rural
}

std::map<int, double> g_ricianKDb_DenseUrban = {
    {10, 6.1},  {20, 13.7}, {30, 12.9}, {40, 10.3}, {50, 9.2},
    {60, 8.4},  {70, 8.0},  {80, 7.4},  {90, 7.6}
};
std::map<int, double> g_ricianKDb_Urban = {
    {10, 40.18}, {20, 23.62}, {30, 12.48}, {40, 8.56}, {50, 7.42},
    {60, 5.97},  {70, 4.88},  {80, 4.22},  {90, 3.81}
};
std::map<int, double> g_ricianKDb_Suburban = {
    {10, 8.9},  {20, 14.0}, {30, 11.3}, {40, 9.0}, {50, 7.5},
    {60, 6.6},  {70, 5.9},  {80, 5.5},  {90, 5.4}
};
std::map<int, double> g_ricianKDb_Rural = {
    {10, 4.63}, {20, 6.83},  {30, 12.91}, {40, 18.9},  {50, 22.44},
    {60, 25.69},{70, 27.95}, {80, 31.45}, {90, 28.01}
};


// Helper function to get 3GPP parameter with linear interpolation
double Get3GPPParameter(const std::map<int, double>& table, double elevation)
{
    int elev_low = static_cast<int>(floor(elevation / 10.0) * 10);
    int elev_high = static_cast<int>(ceil(elevation / 10.0) * 10);
    
    // Clamp to table bounds
    elev_low = std::max(10, std::min(90, elev_low));
    elev_high = std::max(10, std::min(90, elev_high));
    
    if (elev_low == elev_high || elevation <= 10)
    {
        return table.at(elev_low);
    }
    
    // Linear interpolation
    double val_low = table.at(elev_low);
    double val_high = table.at(elev_high);
    double alpha = (elevation - elev_low) / (elev_high - elev_low);
    return val_low + alpha * (val_high - val_low);
}

// Convert geographic coordinates (lat, lon, alt) to ECEF using WGS84
void GeographicToECEF(double lat_deg, double lon_deg, double alt_m, 
                     double& x, double& y, double& z)
{
    // WGS84 ellipsoid parameters
    const double a = 6378137.0;              // Semi-major axis (meters)
    const double e2 = 0.00669437999014;      // First eccentricity squared
    
    double lat_rad = lat_deg * M_PI / 180.0;
    double lon_rad = lon_deg * M_PI / 180.0;
    
    // Radius of curvature in prime vertical
    double N = a / std::sqrt(1.0 - e2 * std::sin(lat_rad) * std::sin(lat_rad));
    
    // ECEF coordinates
    x = (N + alt_m) * std::cos(lat_rad) * std::cos(lon_rad);
    y = (N + alt_m) * std::cos(lat_rad) * std::sin(lon_rad);
    z = (N * (1.0 - e2) + alt_m) * std::sin(lat_rad);
}

// Load satellite trajectory from CSV
bool LoadTrajectory(const std::string& csvFile, const std::string& satName)
{
    std::ifstream file(csvFile);
    if (!file.is_open())
    {
        std::cerr << "ERROR: Cannot open " << csvFile << std::endl;
        return false;
    }
    
    std::string line;
    std::getline(file, line);  // Skip header
    
    double startTime = -1.0;
    int pointCount = 0;
    
    while (std::getline(file, line))
    {
        std::istringstream ss(line);
        std::string field;
        std::vector<std::string> fields;
        
        while (std::getline(ss, field, ','))
        {
            fields.push_back(field);
        }
        
        if (fields.size() < 11) continue;
        
        try
        {
            std::string timestamp = fields[0];
            struct tm tm = {};
            strptime(timestamp.c_str(), "%Y-%m-%d %H:%M:%S", &tm);
            double absTime = timegm(&tm);  
            
            if (startTime < 0) {
                startTime = absTime;
            }
            
            if (fields[1] != satName) continue;
            
            TrajectoryPoint point;
            point.time = absTime - startTime;
            
            double sat_lat = std::stod(fields[5]);
            double sat_lon = std::stod(fields[6]);
            double sat_alt = std::stod(fields[7]) * 1000.0; 

            double real_ecef_x, real_ecef_y, real_ecef_z;
            GeographicToECEF(sat_lat, sat_lon, sat_alt, real_ecef_x, real_ecef_y, real_ecef_z);

            point.x_ecef = real_ecef_x;
            point.y_ecef = real_ecef_y;
            point.z_ecef = real_ecef_z;
            point.csv_distance = std::stod(fields[8]) * 1000.0;
            point.elevation = std::stod(fields[9]);

            g_trajectory.push_back(point);
            pointCount++;
        }
        catch (const std::exception& e)
        {
            std::cerr << "Warning: Error parsing line: " << e.what() << std::endl;
            continue;
        }
    }
    
    file.close();
    
    if (g_trajectory.empty())
    {
        std::cerr << "ERROR: No trajectory data found for " << satName << std::endl;
        return false;
    }
    
    // std::cout << "✓ Loaded " << pointCount << " trajectory points for " << satName << std::endl;
    // std::cout << "  Time span: " << trajectory.front().time << " to " 
    //           << trajectory.back().time << " seconds" << std::endl;
    
    return true;
}

// Interpolate satellite position at given time AND calculate real geometric metrics
void InterpolatePosition(double simTime, double& x, double& y, double& z,
                        double& elevation, double& distance)
{
    if (g_trajectory.empty())
    {
        x = y = z = elevation = distance = 0;
        return;
    }
    
    TrajectoryPoint p1, p2;
    
    // Clamp to trajectory bounds
    if (simTime <= g_trajectory.front().time) {
        p1 = p2 = g_trajectory.front();
    }
    else if (simTime >= g_trajectory.back().time) {
        p1 = p2 = g_trajectory.back();
    }
    else {
        // Find surrounding points
        size_t i = 0;
        while (i < g_trajectory.size() - 1 && g_trajectory[i+1].time < simTime) {
            i++;
        }
        p1 = g_trajectory[i];
        p2 = g_trajectory[i+1];
    }
    
    // 1. ECEF position via linear interpolation (for velocity calculation)
    double alpha = (p1.time == p2.time) ? 0.0 : (simTime - p1.time) / (p2.time - p1.time);
    x = p1.x_ecef + alpha * (p2.x_ecef - p1.x_ecef);
    y = p1.y_ecef + alpha * (p2.y_ecef - p1.y_ecef);
    z = p1.z_ecef + alpha * (p2.z_ecef - p1.z_ecef);
    
    // 2. UE <-> satellite distance (slant range)
    double dx = x - g_groundX;
    double dy = y - g_groundY;
    double dz = z - g_groundZ;
    distance = std::sqrt(dx*dx + dy*dy + dz*dz);
    
    // 3. Elevation
    double ground_r = std::sqrt(g_groundX*g_groundX + g_groundY*g_groundY + g_groundZ*g_groundZ);
    if (ground_r > 0 && distance > 0) {
        double dot_rn = dx*(g_groundX/ground_r) + dy*(g_groundY/ground_r) + dz*(g_groundZ/ground_r);
        elevation = std::asin(std::max(-1.0, std::min(1.0, dot_rn / distance))) * 180.0 / M_PI;
    } else {
        elevation = 0.0;
    }
}

// Calculate Doppler shift from satellite motion
struct DopplerData
{
    double radial_velocity;    // m/s (positive = approaching, negative = receding)
    double doppler_shift_hz;   // Hz at carrier frequency
    double doppler_ppm;        // Parts per million
};

DopplerData CalculateDoppler(double simTime, double carrier_freq_hz)
{
    DopplerData result = {0, 0, 0};
    
    // Get position at current time
    double x1, y1, z1, elev1, dist1;
    InterpolatePosition(simTime, x1, y1, z1, elev1, dist1);
    
    // Get position at time + dt for velocity calculation
    double dt = 1.0;  // 1 second difference
    double x2, y2, z2, elev2, dist2;
    InterpolatePosition(simTime + dt, x2, y2, z2, elev2, dist2);
    
    // Satellite velocity vector (m/s)
    double vx = (x2 - x1) / dt;
    double vy = (y2 - y1) / dt;
    double vz = (z2 - z1) / dt;
    
    // Line-of-sight vector (from ground to satellite)
    double dx = x1 - g_groundX;
    double dy = y1 - g_groundY;
    double dz = z1 - g_groundZ;
    double distance = std::sqrt(dx*dx + dy*dy + dz*dz);
    
    // Normalize line-of-sight vector
    double los_x = dx / distance;
    double los_y = dy / distance;
    double los_z = dz / distance;
    
    // Radial velocity = projection of velocity onto line-of-sight
    // Positive means satellite is approaching (distance decreasing)
    result.radial_velocity = -(vx * los_x + vy * los_y + vz * los_z);
    
    // Doppler shift: Δf = f * v_r / c
    result.doppler_shift_hz = carrier_freq_hz * result.radial_velocity / SPEED_OF_LIGHT;
    result.doppler_ppm = (result.radial_velocity / SPEED_OF_LIGHT) * 1e6;
    
    return result;
}

// Returns interpolated uK (dB) for given scenario and elevation — Ka-band LOS
double GetRicianKFactorDb(const std::string& scenario, double elevation)
{
    std::string s = scenario;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    if (s.find("dense") != std::string::npos)
        return Get3GPPParameter(g_ricianKDb_DenseUrban, elevation);
    if (s.find("urban") != std::string::npos && s.find("suburban") == std::string::npos)
        return Get3GPPParameter(g_ricianKDb_Urban, elevation);
    if (s.find("suburban") != std::string::npos)
        return Get3GPPParameter(g_ricianKDb_Suburban, elevation);
    return Get3GPPParameter(g_ricianKDb_Rural, elevation);  // default: Rural
}

// Update channel conditions based on current satellite position
void UpdateChannelConditions(std::string satelliteName)
{
    double simTime = Simulator::Now().GetSeconds();
    
    // Get interpolated satellite position AND pre-computed distance/elevation from CSV
    double sat_x, sat_y, sat_z, elevation, distance;
    InterpolatePosition(simTime, sat_x, sat_y, sat_z, elevation, distance);
    
    // Use CSV's pre-computed distance (already calculated by Skyfield from Hsinchu)
    // No need to recalculate - Skyfield did it correctly!
    
    // Calculate Doppler shift
    DopplerData doppler = CalculateDoppler(simTime, g_frequency);
    
    // Calculate velocity (simple finite difference for speed metric)
    double sat_x2, sat_y2, sat_z2, elev2, dist2;
    InterpolatePosition(simTime + 1.0, sat_x2, sat_y2, sat_z2, elev2, dist2);
    double dx = sat_x2 - sat_x;
    double dy = sat_y2 - sat_y;
    double dz = sat_z2 - sat_z;
    double speed = std::sqrt(dx*dx + dy*dy + dz*dz);
    
    // Calculate path loss using 3GPP TR 38.811 compliant model
    double distanceKm = distance / 1000.0;
    double frequencyGHz = g_frequency / 1e9;
    
    // 1. Free-space path loss (FSPL)
    double fspl = 32.45 + 20.0 * std::log10(frequencyGHz) + 20.0 * std::log10(distance);
    
    // 2. Atmospheric attenuation
    double atmosLoss = 0.2;  // dB, for clear sky at 20 GHz
    
    // 3. Tropospheric scintillation
    double troposphericLoss = Get3GPPParameter(g_troposphericScintillation, elevation);
    
    // 4. Shadow fading (scenario/LOS-aware)
    static std::default_random_engine generator(g_randomSeed);
    static bool generatorInitialized = false;
    if (!generatorInitialized) {
        generator.seed(g_randomSeed);  // Configurable seed
        generatorInitialized = true;
    }
    std::normal_distribution<double> stdNormal(0.0, 1.0);

    double sfFeature = stdNormal(generator);

    std::string s_lower = scenario;
    if (!s_lower.empty() && s_lower.back() == '\r') s_lower.pop_back();
    std::transform(s_lower.begin(), s_lower.end(), s_lower.begin(), ::tolower);

    bool isUrban = (s_lower.find("urban") != std::string::npos && s_lower.find("suburban") == std::string::npos);
    // std::cout << "Scenario: " << scenario << " (isUrban=" << isUrban << ")" << std::endl;

    // TR 38.811 Table 6.6.1-1: P(LOS) lookup and interpolation
    double plos = 1.0;
    const std::vector<double> elevAngles = {10.0, 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0};
    std::vector<double> probTable;
    if (isUrban) {
        probTable = {0.246, 0.386, 0.493, 0.613, 0.726, 0.805, 0.919, 0.968, 0.992};
    } else {
        probTable = {0.782, 0.869, 0.919, 0.929, 0.935, 0.940, 0.949, 0.952, 0.998};
    }

    double clampedElev = std::max(10.0, std::min(90.0, elevation));
    size_t idx = 0;
    while (idx < elevAngles.size() - 1 && elevAngles[idx + 1] <= clampedElev) { idx++; }
    if (idx == elevAngles.size() - 1) {
        plos = probTable[idx];
    } else {
        double alpha = (clampedElev - elevAngles[idx]) / (elevAngles[idx + 1] - elevAngles[idx]);
        plos = probTable[idx] + alpha * (probTable[idx + 1] - probTable[idx]);
    }
    // std::cout << "plos at elevation " << elevation << "°: " << plos << std::endl;

    double clutterLoss = 0.0;
    double shadowStd = 0.0;

    std::uniform_real_distribution<double> uniformDist(0.0, 1.0);
    bool isLOS = (uniformDist(generator) <= plos);
    
    std::string los_scenario;

    if (isLOS) {
        los_scenario = isUrban ? "Urban-LOS" : "Rural-LOS";
        shadowStd = Get3GPPParameter(GetShadowFadingTable(los_scenario), elevation);
        // std::cout << "LOS scenario: " << los_scenario << ", shadow fading std dev: " << shadowStd << " dB" << std::endl;
    } else {
        los_scenario = isUrban ? "Urban-NLOS" : "Rural-NLOS";
        clutterLoss = Get3GPPParameter(GetClutterLossTable(los_scenario), elevation);
        shadowStd = Get3GPPParameter(GetShadowFadingTable_NLOS(los_scenario), elevation);
        // std::cout << "NLOS scenario: " << los_scenario << ", clutter loss: " << clutterLoss << " dB, shadow fading std dev: " << shadowStd << " dB" << std::endl;
    }

    double shadowFading = sfFeature * shadowStd;

    // 5. Small-scale fading (Rician)
    double kFactorDb = GetRicianKFactorDb(s_lower, elevation);
    double K = pow(10.0, kFactorDb/10.0);
    std::normal_distribution<double> gauss(0.0,1.0);
    double sigma = sqrt(1.0/(2*(K+1)));
    double s_val = sqrt(K/(K+1));
    double x = sigma * gauss(generator);
    double y = sigma * gauss(generator);
    double r = sqrt(pow(s_val + x,2) + pow(y,2));
    double ricianFading = -20 * log10(r);
    
    // Total path loss (3GPP compliant)
    double totalPathLoss = fspl + atmosLoss + troposphericLoss + clutterLoss + shadowFading + ricianFading;
    
    // Received power
    double rxPower = g_txPower + g_txAntennaGain + g_rxGain - totalPathLoss;
    
    // Noise power
    double noisePowerDbm = -174.0 + 10.0 * std::log10(g_bandwidth) + g_noiseFigure;
    
    // Doppler-induced SNR degradation
    // Model: Imperfect frequency compensation causes SNR loss
    // Assume residual Doppler error = 1% of total Doppler (after pre-compensation)
    // SNR loss increases with residual frequency offset
    double residualDopplerHz = std::abs(doppler.doppler_shift_hz) * 0.01;  // 1% residual
    double symbolRate = g_bandwidth;  // Approximate symbol rate ~ bandwidth
    double frequencyOffsetRatio = residualDopplerHz / symbolRate;
    
    // SNR degradation formula (empirical): loss ≈ -10*log10(sinc^2(π*Δf/Rs))
    // Simplified: loss ≈ 0.5 dB per 1% frequency offset for moderate offsets
    double dopplerSnrLoss = 0.0;
    if (frequencyOffsetRatio > 0.001) {  // Only if offset > 0.1%
        dopplerSnrLoss = 10.0 * std::log10(1.0 + frequencyOffsetRatio * 100.0);
    }
    
    // SNR (including Doppler degradation)
    double snrDb = rxPower - noisePowerDbm + g_fecCodingGain - dopplerSnrLoss;
    
    // BER from SNR (BPSK)
    double snrLinear = std::pow(10.0, snrDb / 10.0);
    double ber = 0.5 * std::erfc(std::sqrt(snrLinear));
    
    // PER
    uint32_t bitsPerPacket = g_packetSize * 8;
    double per = 1.0 - std::pow(1.0 - ber, bitsPerPacket);
    if (per < 0.0) per = 0.0;
    if (per > 1.0) per = 1.0;
    
    // Update error models
    g_uplinkErrorModel->SetAttribute("ErrorRate", DoubleValue(per));
    g_downlinkErrorModel->SetAttribute("ErrorRate", DoubleValue(per));
    
    // Update propagation delay based on distance
    // Delay = Distance / Speed_of_Light (one-way)
    double propagationDelay = distance / SPEED_OF_LIGHT;  // seconds
    if (g_p2pChannel)
    {
        g_p2pChannel->SetAttribute("Delay", TimeValue(Seconds(propagationDelay)));
    }
    
    // Log to files
    g_channelLog << simTime << ","
                 << satelliteName << ","
                 << distanceKm << ","
                 << elevation << ","
                 << speed << ","
                 << sat_x / 1000.0 << ","
                 << sat_y / 1000.0 << ","
                 << sat_z / 1000.0 << std::endl;
    
    g_snrLog << simTime << ","
             << distanceKm << ","
             << elevation << ","
             << totalPathLoss << ","
             << rxPower << ","
             << snrDb << ","
             << ber << ","
             << per * 100.0 << std::endl;
    
    // Log full channel data with PDR
    // Calculate PDR from FlowMonitor statistics
    double pdr = 100.0;  // Default to 100% if no packets yet
    if (g_flowMonitor != nullptr)
    {
        std::map<FlowId, FlowMonitor::FlowStats> stats = g_flowMonitor->GetFlowStats();
        uint64_t totalTx = 0;
        uint64_t totalRx = 0;
        for (auto& flow : stats)
        {
            totalTx += flow.second.txPackets;
            totalRx += flow.second.rxPackets;
        }
        
        // Calculate PDR for this second (delta from previous)
        uint64_t deltaTx = totalTx - g_prevTxPackets;
        uint64_t deltaRx = totalRx - g_prevRxPackets;
        
        if (deltaTx > 0)
        {
            pdr = (double)deltaRx / deltaTx * 100.0;
        }
        
        // Update previous values
        g_prevTxPackets = totalTx;
        g_prevRxPackets = totalRx;
    }
    
    // Calculate propagation delays
    // UE to Satellite delay (one-way)
    double ueSatDelayMs = (distance / SPEED_OF_LIGHT) * 1000.0;  // milliseconds
    
    // Satellite to Core Network delay (core network is at the same position as UE)
    // So the distance is the same as UE-Satellite distance
    double satCnDelayMs = (distance / SPEED_OF_LIGHT) * 1000.0;  // milliseconds
    
    g_fullLog << simTime << ","
               << distanceKm << ","
               << elevation << ","
               << speed << ","
               << sat_x / 1000.0 << ","
               << sat_y / 1000.0 << ","
               << sat_z / 1000.0 << ","
               << per * 100.0 << ","
               << pdr << ","
               << ueSatDelayMs << ","
               << satCnDelayMs << std::endl;
    
    // Log Doppler data
    g_dopplerLog << simTime << ","
                 << distanceKm << ","
                 << elevation << ","
                 << doppler.radial_velocity / 1000.0 << ","  // km/s
                 << doppler.doppler_shift_hz / 1000.0 << ","  // kHz
                 << doppler.doppler_ppm << ","
                 << residualDopplerHz << ","  // Hz
                 << dopplerSnrLoss << std::endl;  // dB
    
    // Print periodic updates
    if ((int)simTime % 10 == 0)
    {
        double delay_ms = (distance / SPEED_OF_LIGHT) * 1000.0;  // one-way delay in ms
        std::cout << "[t=" << simTime << "s] "
                  << "Dist=" << distanceKm << "km, "
                  << "Delay=" << delay_ms << "ms, "
                  << "Elev=" << elevation << "°, "
                  << "Doppler=" << doppler.doppler_shift_hz/1000.0 << "kHz, "
                  << "SNR=" << snrDb << "dB, "
                  << "PER=" << per*100.0 << "%" << std::endl;
    }
    
    // Schedule next update
    Simulator::Schedule(Seconds(1.0), &UpdateChannelConditions, satelliteName);
}

int
main(int argc, char* argv[])
{
    // Simulation parameters
    std::string csvFile = "dataset/visible_satellites_hsinchu.csv";
    std::string satelliteName = "STARLINK-31077";  // Start with close satellite
    double simTime = 1200.0;
    double packetInterval = 0.001;
    
    // NTN channel parameters (3GPP TR 38.811 compliant)
    g_frequency = 20.0e9;     // 20 GHz Ka-band
    g_txPower = 19.5;        // 19.5 dBm - TX amplifier output power
    g_txAntennaGain = 40;   // 30.5 dBi - Satellite antenna power gain
    g_rxGain = 39.7;   // 39.7 dBi - VAST user terminal antenna
    g_noiseFigure = 1.2;         // 1.2 dB - VAST 
    g_bandwidth = 250e6;      // 250 MHz per resource block
    g_fecCodingGain = 5.0;    // 3 dB - realistic FEC gain
    g_packetSize = 625;       // 625 bytes (5000 bits) - typical for NTN payloads
    
    // Parse command line
    CommandLine cmd;
    cmd.AddValue("csvFile", "Path to SGP4 CSV file", csvFile);
    cmd.AddValue("satellite", "Satellite name to track", satelliteName);
    cmd.AddValue("simTime", "Simulation time (seconds)", simTime);
    cmd.AddValue("txPower", "Transmit power (dBm)", g_txPower);
    cmd.AddValue("fecGain", "FEC coding gain (dB)", g_fecCodingGain);
    cmd.AddValue("seed", "Random seed for reproducibility", g_randomSeed);
    cmd.AddValue("scenario", "Propagation scenario (Rural, Urban, Suburban, DenseUrban)", scenario);
    cmd.Parse(argc, argv);
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "SGP4 NTN UDP Example" << std::endl;
    std::cout << "3GPP TR 38.811 Compliant" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Satellite: " << satelliteName << std::endl;
    std::cout << "Frequency: " << g_frequency/1e9 << " GHz (Ka-band)" << std::endl;
    std::cout << "TX Power: " << g_txPower << " dBm (" << std::pow(10, (g_txPower-30)/10) << " W)" << std::endl;
    std::cout << "Noise Figure: " << g_noiseFigure << " dB" << std::endl;
    std::cout << "Bandwidth: " << g_bandwidth/1e6 << " MHz" << std::endl;
    std::cout << "FEC Gain: " << g_fecCodingGain << " dB" << std::endl;
    std::cout << "Scenario: " << scenario << " (3GPP)" << std::endl;
    std::cout << "========================================\n" << std::endl;

    // Convert ground station position to ECEF
    // Hsinchu, Taiwan coordinates (matching SatTrack.py: 24.8°N, 120.97°E, 0m)
    GeographicToECEF(24.8, 120.97, 0.0, g_groundX, g_groundY, g_groundZ);
    
    std::cout << "Ground station (Hsinchu, Taiwan):" << std::endl;
    std::cout << "  Lat/Lon: 24.8°N, 120.97°E, 0m alt" << std::endl;
    std::cout << "  ECEF: (" << g_groundX/1000.0 << ", " 
              << g_groundY/1000.0 << ", " << g_groundZ/1000.0 << ") km" << std::endl;
    
    // Load satellite trajectory
    if (!LoadTrajectory(csvFile, satelliteName))
    {
        return 1;
    }
    
    // Verify initial distance using CSV values
    double sat_x, sat_y, sat_z, elevation, distance;
    InterpolatePosition(0.0, sat_x, sat_y, sat_z, elevation, distance);
    
    std::cout << "\nInitial satellite position:" << std::endl;
    std::cout << "  GCRS Position: (" << sat_x/1000.0 << ", " 
              << sat_y/1000.0 << ", " << sat_z/1000.0 << ") km" << std::endl;
    std::cout << "  Distance: " << distance/1000.0 << " km" << std::endl;
    std::cout << "  Elevation: " << elevation << "°" << std::endl;
    
    if (elevation < 10.0) {
        std::cout << "  ⚠ WARNING: Satellite is below 10° elevation at t=0." << std::endl;
        std::cout << "  3GPP channel models may clamp values or behave unexpectedly for very low elevations." << std::endl;
    }

    if (distance/1000.0 < 3000.0)
    {
        std::cout << "  ✓ Using Skyfield-computed distance/elevation" << std::endl;
    }
    else
    {
        std::cout << "  ⚠ Warning: Distance seems large (> 3000 km)" << std::endl;
    }
    
    // Create nodes
    NodeContainer groundNodes, satNodes;
    groundNodes.Create(1);
    satNodes.Create(1);
    
    // Calculate initial propagation delay based on satellite distance
    // Speed of light = 299,792,458 m/s
    const double SPEED_OF_LIGHT = 299792458.0;
    double initialDelay = distance / SPEED_OF_LIGHT;  // seconds (one-way)
    
    std::cout << "  Initial propagation delay: " << initialDelay * 1000.0 << " ms (one-way)" << std::endl;
    std::cout << "  Round-trip time (RTT): " << initialDelay * 2000.0 << " ms\n" << std::endl;
    
    // Create P2P link with realistic delay
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    p2p.SetChannelAttribute("Delay", TimeValue(Seconds(initialDelay)));
    NetDeviceContainer devices = p2p.Install(groundNodes.Get(0), satNodes.Get(0));
    
    // Get the P2P channel for dynamic delay updates
    Ptr<PointToPointNetDevice> p2pDevice = DynamicCast<PointToPointNetDevice>(devices.Get(0));
    g_p2pChannel = DynamicCast<PointToPointChannel>(p2pDevice->GetChannel());
    
    // Install Internet stack
    InternetStackHelper internet;
    internet.Install(groundNodes);
    internet.Install(satNodes);
    
    // Assign IP addresses
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = ipv4.Assign(devices);
    
    // Set up UDP echo server on ground
    uint16_t port = 9;
    UdpEchoServerHelper echoServer(port);
    ApplicationContainer serverApps = echoServer.Install(groundNodes.Get(0));
    serverApps.Start(Seconds(0.0));
    serverApps.Stop(Seconds(simTime));
    
    // Set up UDP echo client on satellite
    UdpEchoClientHelper echoClient(interfaces.GetAddress(0), port);
    echoClient.SetAttribute("MaxPackets", UintegerValue(uint32_t(simTime / packetInterval) + 10));
    echoClient.SetAttribute("Interval", TimeValue(Seconds(packetInterval)));
    echoClient.SetAttribute("PacketSize", UintegerValue(g_packetSize));
    
    ApplicationContainer clientApps = echoClient.Install(satNodes.Get(0));
    clientApps.Start(Seconds(0.0));
    clientApps.Stop(Seconds(simTime));
    
    // Create error models
    g_uplinkErrorModel = CreateObject<RateErrorModel>();
    g_uplinkErrorModel->SetAttribute("ErrorUnit", EnumValue(RateErrorModel::ERROR_UNIT_PACKET));
    
    g_downlinkErrorModel = CreateObject<RateErrorModel>();
    g_downlinkErrorModel->SetAttribute("ErrorUnit", EnumValue(RateErrorModel::ERROR_UNIT_PACKET));
    
    // Apply to both devices
    devices.Get(0)->SetAttribute("ReceiveErrorModel", PointerValue(g_downlinkErrorModel));
    devices.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(g_uplinkErrorModel));
    
    std::cout << "\n✓ Bidirectional error models configured" << std::endl;
    std::cout << "✓ Dynamic channel updates every 1 second\n" << std::endl;

    std::string outputDir = "result/single-satellite-udp";
    std::filesystem::create_directories(outputDir);
    
    // Set up flow monitor
    FlowMonitorHelper flowmon;
    g_flowMonitor = flowmon.InstallAll();
    g_flowClassifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    
    // Open log files (filenames include satellite name)
    g_channelLog.open(outputDir + "/" + satelliteName + "-" + scenario + "-channel-log.csv");
    g_channelLog << "time_s,satellite,distance_km,elevation_deg,speed_m_s,x_km,y_km,z_km" << std::endl;
    
    g_snrLog.open(outputDir + "/" + satelliteName + "-" + scenario + "-snr-log.csv");
    g_snrLog << "time_s,distance_km,elevation_deg,path_loss_dB,rx_power_dBm,snr_dB,ber,per_percent" << std::endl;
    
    g_dopplerLog.open(outputDir + "/" + satelliteName + "-" + scenario + "-doppler-log.csv");
    g_dopplerLog << "time_s,distance_km,elevation_deg,radial_velocity_km_s,doppler_shift_kHz,doppler_ppm,residual_doppler_Hz,doppler_snr_loss_dB" << std::endl;

    g_fullLog.open(outputDir + "/" + satelliteName + "-" + scenario + "-full-log.csv");
    g_fullLog << "time_s,distance_km,elevation_deg,speed_m_s,x_km,y_km,z_km,per,pdr,ue_sat_delay_ms,sat_cn_delay_ms" << std::endl;
    
    // Schedule channel updates
    Simulator::Schedule(Seconds(0.0), &UpdateChannelConditions, satelliteName);
    
    // Run simulation
    std::cout << "Starting simulation for " << simTime << " seconds...\n" << std::endl;
    
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    
    // Print statistics
    std::cout << "\n========================================" << std::endl;
    std::cout << "=== Flow Statistics ===" << std::endl;
    std::cout << "========================================" << std::endl;
    
    g_flowMonitor->CheckForLostPackets();
    std::map<FlowId, FlowMonitor::FlowStats> stats = g_flowMonitor->GetFlowStats();
    
    for (auto i = stats.begin(); i != stats.end(); ++i)
    {
        Ipv4FlowClassifier::FiveTuple t = g_flowClassifier->FindFlow(i->first);
        std::string direction = (t.sourceAddress == interfaces.GetAddress(1)) ?
                                "Downlink (Sat→Ground)" : "Uplink (Ground→Sat)";
        
        std::cout << "\nFlow " << i->first << ": " << direction << std::endl;
        std::cout << "  Tx Packets: " << i->second.txPackets << std::endl;
        std::cout << "  Rx Packets: " << i->second.rxPackets << std::endl;
        std::cout << "  Lost Packets: " << i->second.lostPackets << std::endl;
        
        if (i->second.txPackets > 0)
        {
            double pdr = (double)i->second.rxPackets / i->second.txPackets * 100.0;
            std::cout << "  📊 PDR: " << pdr << "%" << std::endl;
        }
        
        if (i->second.rxPackets > 0)
        {
            std::cout << "  Mean Delay: " 
                     << i->second.delaySum.GetMilliSeconds() / i->second.rxPackets << " ms" << std::endl;
        }
        
        std::cout << "  Throughput: " 
                 << i->second.rxBytes * 8.0 / simTime / 1000.0 << " kbps" << std::endl;
    }
    
    g_channelLog.close();
    g_snrLog.close();
    g_dopplerLog.close();
    g_fullLog.close();
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "=== Output Files ===" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Channel log: " << outputDir << "/" << satelliteName << "-" << scenario << "-channel-log.csv" << std::endl;
    std::cout << "SNR log:     " << outputDir << "/" << satelliteName << "-" << scenario << "-snr-log.csv" << std::endl;
    std::cout << "Doppler log: " << outputDir << "/" << satelliteName << "-" << scenario << "-doppler-log.csv" << std::endl;
    std::cout << "Full log:    " << outputDir << "/" << satelliteName << "-" << scenario << "-full-log.csv" << std::endl;
    std::cout << "\n✓ Simulation complete!" << std::endl;
    
    Simulator::Destroy();
    
    return 0;
}
