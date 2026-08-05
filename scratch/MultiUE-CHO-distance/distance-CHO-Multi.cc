#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/propagation-module.h"
#include "ns3/spectrum-module.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <cmath>
#include <random>
#include <algorithm>
#include <sys/stat.h>
#include <sys/types.h>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("Sgp4NtnMultiSatelliteHandover");

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

struct SatelliteState
{
    std::string name;
    std::vector<TrajectoryPoint> trajectory;
};


struct UeSatMetrics
{
    double elevation;    // degrees above horizon (negative = below horizon)
    double distance;     // slant range in meters
    double dopplerShift; // Hz at carrier frequency
    double snrDb;        // SNR in dB
};


struct MeasResultCell
{
    std::string cellId;
    double rsrp;
    double rsrq;
    double sinr;
    double distance;
    double elevation;
    double dopplerShift;
    double measurementTime;
};

struct MeasurementReport
{
    double reportTime;
    MeasResultCell servingCell;
    std::vector<MeasResultCell> neighborCells;
    std::string eventType;
    std::string triggerReason;
    bool servingCellBelowThreshold;
    bool betterNeighborAvailable;
};

struct UeMeasurementConfig
{
    double measurementPeriod;
    double timeToTrigger;
    double a3Offset;
    double hysteresis;
    double maxDistanceThreshold;
    double minElevationThreshold;
    double sinrThreshold;
};

struct UeMeasurementState
{
    std::string servingCellId;
    double lastMeasurementTime;
    bool triggerActive;
    double triggerStartTime;
    std::string candidateTargetCell;
    std::map<std::string, MeasResultCell> lastMeasurements;
    int reportsSent;
    double lastReportTime;
};


struct ChoExecutionCondition
{
    std::string eventType;
    double a3Offset;
    double a3Hysteresis;
    double maxDistanceThreshold;
    double minElevationThreshold;
    double minSinrThreshold;
    double timeToTrigger;
};

struct ChoConfiguration
{
    std::string targetCellId;
    double configurationTime;
    ChoExecutionCondition executionCondition;
    bool isValid;
    double expiryTime;
    bool conditionMet;
    double conditionMetTime;
};

enum ChoPhase {
    CHO_IDLE,
    CHO_MEAS_REPORT_UPLINK,
    CHO_GNB_CHO_DECISION,
    CHO_XN_REQUEST,
    CHO_XN_RESPONSE,
    CHO_RRC_RECONFIG_DOWNLINK,
    CHO_CONFIG_STORED,
    CHO_UE_EVALUATING,
    CHO_UE_EXECUTING,
    CHO_XN_SUCCESS,
    CHO_XN_CANCEL
};

// Track which CHO phase the UE has reached
struct UeChoProcedureState {
    ChoPhase phase;
    std::string sourceCell;
    std::string targetCell;
    double procedureStartTime;
    double phaseStartTime;
    double servingSnrAtReport;
    double targetSnrAtReport;
    double configReceivedTime;
    double configExpiryTime;
    double executionConditionMetTime;
    double executionStartTime;
    double servingSnrAtExecution;
    double targetSnrAtExecution;
    bool executionTttActive;
    double executionTttStartTime;
    double executionTimeToTrigger;
    double measReportUplinkDelay;
    double reconfigDownlinkDelay;
    bool failed;
    std::string failureReason;
};

// Record the Config and statistics on the UE
struct UeChoConfigState {
    std::map<std::string, ChoConfiguration> choConfigs;
    bool executionPending;
    std::string executionTargetCell;
    double executionTriggerTime;
    int choConfigsReceived;
    int choExecutionsTriggered;
    int choConfigsExpired;
};



enum UeRrcState {
    UE_CONNECTED,
    UE_HO_PREPARING,
    UE_HO_EXECUTING,
    UE_HO_COMPLETING
};

struct UeContext {
    std::string servingGnb;
    std::string targetGnb;
    UeRrcState rrcState;
    double lastMeasurementTime;
    double lastHandoverTime;
    bool mobilityRestricted;
};

enum HandoverPhase {
    PHASE_IDLE,
    PHASE_PREPARATION,
    PHASE_EXECUTION,
    PHASE_COMPLETION
};

struct HandoverState {
    HandoverPhase currentPhase;
    std::string sourceGnb;
    std::string targetGnb;
    double startTime;
    double phaseStartTime;
    double rachStartTime;
    int lostPackets;
    bool interruptionActive;
};

struct HoContext {
    double preServingSnr;
    double preNeighborSnr;
    double decisionTime;
    bool valid;
    std::string sourceCellId;
    std::string targetCellId;
    std::string triggerType;
};

struct PendingPredictionEval {
    int ueId;
    std::string servingCell;
    std::string neighborCell;
    double decisionTime;
    double evalTime;
};


struct UeInfo
{
    // [1] Basic attributes and geographic coordinates (read from CSV)
    int simId;
    std::string groupName;
    std::string groupId;
    int numUsers;
    double lat;
    double lon;
    std::string scenario;
    double ecefX, ecefY, ecefZ;

    // [2] Basic connection state
    UeContext context;
    HandoverState hoState;

    // [3] Measurement state
    UeMeasurementConfig measConfig;
    UeMeasurementState measState; 

    // [4] CHO-specific core state (each UE has its own state machine)
    UeChoProcedureState choProcState;
    UeChoConfigState choConfigState;
    HoContext hoContext;

    // [5] Statistics
    int hoCount = 0;
    int hoFail = 0;

    // [6] NS-3 network and physical layer objects
    Ptr<RateErrorModel> ulErrorModel;
    Ptr<RateErrorModel> dlErrorModel;
    Ptr<PointToPointChannel> p2pChannel;

    // [7] Physical layer cache 
    std::map<std::string, std::pair<double, UeSatMetrics>> channelCache;
    std::map<std::string, double> shadowFadingCache;
    std::map<std::string, bool> losStateCache;
    std::map<std::string, double> lastEvalElev;

    // Independent random number generator
    std::default_random_engine rng;
};


// ==================================================================================
// 6. Global Variables Definition
// ==================================================================================

// All UEs are stored in this array, accessed via g_ues[ueId]
std::vector<UeInfo> g_ues;

// Satellite orbits are shared, so keep as global variable
std::map<std::string, SatelliteState> g_satellites;
std::vector<std::string> g_satelliteOrder;

// CHO global parameter configuration (all UEs use the same set of rules)
struct ChoNtnConfig {
    double configValidityPeriod;
    double executionEvaluationPeriod;
    double executionTimeToTrigger;
    double minViableSinr;
    double maxViableDistance;
};

ChoNtnConfig g_choNtnConfig = {
    60.0,    // configValidityPeriod: 60 seconds
    0.5,     // executionEvaluationPeriod: 500 ms
    1.0,     // executionTimeToTrigger: 1.0 second (default)
    0.0,     // minViableSinr: 0 dB
    2500.0   // maxViableDistance: 2500 km
};

// Aggregate statistics to track overall CHO system performance
struct ChoNtnStatistics {
    uint32_t totalReports;
    uint32_t configsSent;
    uint32_t configsReceived;
    uint32_t executionsTriggered;
    uint32_t successfulHandovers;
    uint32_t failedAtExecution;
    uint32_t configsExpired;
    double totalProcedureDelay;
    double avgProcedureDelay;
    double totalExecutionDelay;
    double avgExecutionDelay;
    double lastReportTime;
};

ChoNtnStatistics g_choNtnStats = {0, 0, 0, 0, 0, 0, 0, 0.0, 0.0, 0.0, 0.0, -999.0};

void ResetHoContext(HoContext& hoContext)
{
    hoContext.preServingSnr = -999.0;
    hoContext.preNeighborSnr = -999.0;
    hoContext.decisionTime = -1.0;
    hoContext.valid = false;
    hoContext.sourceCellId.clear();
    hoContext.targetCellId.clear();
    hoContext.triggerType.clear();
}

// Basic parameters and counters
int g_handoverCount = 0;
uint32_t g_randomSeed = 45;
double g_reportingOffset = 1.0;
double g_minSNR = 0.0;
double g_handoverHysteresis = 3.0;
double g_timeToTrigger = 1.0;
double g_maxDistance = 1500.0;
double g_choExecutionOffset = 3.0;
double g_measurementPeriod = 1.0;
double g_predictionEvalDelay = 5.0;

// NTN Failure Thresholds
const double MIN_SNR_FOR_RRC_SIGNALING = 0.0;

// Processing Delays
const double DELAY_RRC_PROC = 0.010;
const double DELAY_XN_PROC = 0.005;
const double DELAY_AMF_PROC = 0.020;
const double DELAY_BEAM_SWITCH = 0.005;

// Logging streams
std::ofstream g_handoverLog;
std::ofstream g_xnSignalingLog;      // New: Xn interface signaling log
std::ofstream g_allSatellitesLog;
std::ofstream g_channelLog;
std::ofstream g_snrLog;
std::ofstream g_choLog;              // CHO detailed event log
std::vector<PendingPredictionEval> g_pendingPredictionEvals;

Ptr<RateErrorModel> g_uplinkErrorModel;
Ptr<RateErrorModel> g_downlinkErrorModel;
Ptr<PointToPointChannel> g_p2pChannel;

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
uint32_t g_packetSize;
double g_txAntennaGain;
double g_rxGain;

// Global flow monitor for link quality tracking
Ptr<FlowMonitor> g_flowMonitor;
uint32_t g_lastTxPackets = 0;
uint32_t g_lastRxPackets = 0;

// Forward declaration
void InterpolatePosition(const std::string& satName, double simTime, 
                        double& x, double& y, double& z,
                        double& elevation, double& distance);

// ISL (Inter-Satellite Link) delay calculation
// Assumes ISL connectivity between satellites in same constellation
double GetIslDelay(const std::string& sat1, const std::string& sat2, double simTime)
{
    if (sat1 == sat2) return 0.0;

    double x1, y1, z1, el1, d1;
    double x2, y2, z2, el2, d2;
    InterpolatePosition(sat1, simTime, x1, y1, z1, el1, d1);
    InterpolatePosition(sat2, simTime, x2, y2, z2, el2, d2);

    double dx = x1 - x2;
    double dy = y1 - y2;
    double dz = z1 - z2;
    double dist = std::sqrt(dx*dx + dy*dy + dz*dz);
    
    // One-way propagation delay at speed of light
    return dist / SPEED_OF_LIGHT;
}

// Uu interface delay (satellite ↔ UE) - ONE-WAY propagation delay
double GetUuDelay(const std::string& satellite, double simTime)
{
    double x, y, z, el, dist;
    InterpolatePosition(satellite, simTime, x, y, z, el, dist);
    
    // One-way propagation delay
    return dist / SPEED_OF_LIGHT;
}


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

// 3GPP TR 38.811 Table 6.6.2-1: Shadow fading std dev σ_SF (dB), Ka-band LOS
// Source: SFCL_SuburbanRural[elev][Ka_LOS_sigF] in three-gpp-propagation-loss-model.cc
// Rural — SFCL_SuburbanRural, Ka_LOS_sigF column
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

// Discover satellites with peak elevation above threshold
std::vector<std::string> DiscoverSatellites(const std::string& csvFile, double minElevation, double simTime)
{
    std::ifstream file(csvFile);
    
    // Structure to track satellite info for intelligent selection
    struct SatelliteInfo {
        std::string name;
        double peakElevation;
        double firstAppearanceTime;
        double lastSeenTime;
        double minDistance;
        int visibilityCount;
    };
    
    std::map<std::string, SatelliteInfo> satelliteMap;
    
    if (!file.is_open())
    {
        std::cerr << "ERROR: Cannot open " << csvFile << " for satellite discovery" << std::endl;
        return {};
    }
    
    std::string line;
    std::getline(file, line);  // Skip header
    
    double startTime = -1.0;
    
    while (std::getline(file, line))
    {
        std::istringstream ss(line);
        std::string field;
        std::vector<std::string> fields;
        
        while (std::getline(ss, field, ','))
        {
            fields.push_back(field);
        }
        
        if (fields.size() < 5) continue;  // Need timestamp, name, elevation, distance
        
        try
        {
            // Parse timestamp
            std::string timestamp = fields[0];
            struct tm tm = {};
            strptime(timestamp.c_str(), "%Y-%m-%d %H:%M:%S", &tm);
            double absTime = timegm(&tm);
            
            if (startTime < 0) startTime = absTime;
            double relativeTime = absTime - startTime;
            
            // Only consider satellites visible within simulation time window
            if (relativeTime > simTime) break;
            
            std::string satName = fields[1];
            double elevation = std::stod(fields[2]);
            double distance = std::stod(fields[3]);  // Assuming distance is in column 4
            
            // Initialize or update satellite info
            if (satelliteMap.find(satName) == satelliteMap.end())
            {
                SatelliteInfo info;
                info.name = satName;
                info.peakElevation = elevation;
                info.firstAppearanceTime = relativeTime;
                info.lastSeenTime = relativeTime;
                info.minDistance = distance;
                info.visibilityCount = 1;
                satelliteMap[satName] = info;
            }
            else
            {
                auto& info = satelliteMap[satName];
                if (elevation > info.peakElevation) info.peakElevation = elevation;
                if (distance < info.minDistance) info.minDistance = distance;
                info.lastSeenTime = relativeTime;
                info.visibilityCount++;
            }
        }
        catch (const std::exception&)
        {
            continue;
        }
    }
    file.close();
    
    // Filter satellites by peak elevation
    std::vector<SatelliteInfo> candidates;
    for (const auto& pair : satelliteMap)
    {
        if (pair.second.peakElevation >= minElevation)
        {
            candidates.push_back(pair.second);
        }
    }
    
    // Return ALL satellites that meet elevation criteria
    // (Previously limited to 12 satellites, causing severe coverage gaps)
    std::vector<std::string> result;
    for (const auto& sat : candidates)
    {
        result.push_back(sat.name);
    }
    
    std::cout << "✓ Discovered " << result.size() << " satellites with peak elevation ≥ " 
              << minElevation << "° during first " << simTime << "s" << std::endl;
    
    return result;
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
    std::vector<TrajectoryPoint> trajectory;
    
    while (std::getline(file, line))
    {
        std::istringstream ss(line);
        std::string field;
        std::vector<std::string> fields;
        
        while (std::getline(ss, field, ',')) fields.push_back(field);
        
        if (fields.size() < 11) continue;
        
        try
        {
            std::string timestamp = fields[0];
            struct tm tm = {};
            strptime(timestamp.c_str(), "%Y-%m-%d %H:%M:%S", &tm);
            double absTime = timegm(&tm);  
            
            if (startTime < 0) startTime = absTime;
            if (fields[1] != satName) continue;
            
            TrajectoryPoint point;
            point.time = absTime - startTime;
            
            double sat_lat = std::stod(fields[5]);
            double sat_lon = std::stod(fields[6]);
            double sat_alt = std::stod(fields[7]) * 1000.0;
            
            GeographicToECEF(sat_lat, sat_lon, sat_alt, point.x_ecef, point.y_ecef, point.z_ecef);
            trajectory.push_back(point);
        }
        catch (const std::exception& e) { continue; }
    }
    file.close();
    
    if (trajectory.empty()) return false;
    
    SatelliteState satState;
    satState.name = satName;
    satState.trajectory = trajectory;
    
    g_satellites[satName] = satState;
    return true;
}

// Load UE groups from simulation_groups CSV and populate g_ues
bool LoadUeGroupsFromCsv(const std::string& csvFile)
{
    std::ifstream file(csvFile);
    if (!file.is_open()) return false;

    std::string line;
    std::getline(file, line);  // Skip header row

    while (std::getline(file, line))
    {
        if (line.empty()) continue;

        std::istringstream ss(line);
        std::string field;
        std::vector<std::string> fields;
        while (std::getline(ss, field, ',')) fields.push_back(field);

        if (fields.size() < 7) continue;

        UeInfo ue;
        ue.simId     = std::stoi(fields[0]);
        ue.groupName = fields[1];
        ue.groupId   = fields[2];
        ue.numUsers  = std::stoi(fields[3]);
        ue.lat       = std::stod(fields[4]);
        ue.lon       = std::stod(fields[5]);
        ue.scenario  = fields[6];
        ue.rng.seed(g_randomSeed + g_ues.size());  // Dynamic seed from command-line parameter

        // Calculate ECEF coordinates specific to this UE group
        GeographicToECEF(ue.lat, ue.lon, 0.0, ue.ecefX, ue.ecefY, ue.ecefZ);

        // Initialize basic and measurement states
        ue.measState = {"", -1.0, false, -1.0, "", {}, 0, -1.0};
        ue.context = {"", "", UE_CONNECTED, 0.0, 0.0, false};
        ue.hoState = {PHASE_IDLE, "", "", 0.0, 0.0, 0.0, 0, false};
        ue.measConfig = {1.0, g_timeToTrigger, 0.0, g_handoverHysteresis, g_maxDistance, 0.0, g_minSNR};

        // ★ Initialize CHO-specific state ★
        ue.choProcState = {CHO_IDLE, "", "", 0.0, 0.0, 0.0, 0.0, -999.0, -999.0, -999.0, -999.0, 0.0, 0.0, false, -1.0, g_choNtnConfig.executionTimeToTrigger, 0.0, 0.0, false, ""};
        ue.choConfigState = {{}, false, "", -1.0, 0, 0, 0};
        ue.hoContext = {-999.0, -999.0, -1.0, false, "", "", ""};

        g_ues.push_back(ue);
    }
    file.close();
    return true;
}

// Interpolate satellite position at given time for specific satellite
void InterpolatePosition(const std::string& satName, double simTime, 
                        double& x, double& y, double& z,
                        double& elevation, double& distance)
{
    auto it = g_satellites.find(satName);
    if (it == g_satellites.end() || it->second.trajectory.empty())
    {
        x = y = z = elevation = distance = 0;
        return;
    }
    
    const std::vector<TrajectoryPoint>& trajectory = it->second.trajectory;
    
    // Clamp to trajectory bounds
    if (simTime <= trajectory.front().time)
    {
        x = trajectory.front().x_ecef;
        y = trajectory.front().y_ecef;
        z = trajectory.front().z_ecef;
        elevation = 0.0;  // Will be computed on demand
        distance = 0.0;   // Will be computed on demand
        return;
    }
    
    if (simTime >= trajectory.back().time)
    {
        x = trajectory.back().x_ecef;
        y = trajectory.back().y_ecef;
        z = trajectory.back().z_ecef;
        elevation = 0.0;  // Will be computed on demand
        distance = 0.0;   // Will be computed on demand
        return;
    }
    
    // Find surrounding points
    size_t i = 0;
    while (i < trajectory.size() - 1 && trajectory[i+1].time < simTime)
    {
        i++;
    }
    
    // Linear interpolation
    const TrajectoryPoint& p1 = trajectory[i];
    const TrajectoryPoint& p2 = trajectory[i+1];
    
    double alpha = (simTime - p1.time) / (p2.time - p1.time);
    
    x = p1.x_ecef + alpha * (p2.x_ecef - p1.x_ecef);
    y = p1.y_ecef + alpha * (p2.y_ecef - p1.y_ecef);
    z = p1.z_ecef + alpha * (p2.z_ecef - p1.z_ecef);
    elevation = 0.0;  // Will be computed on demand
    distance = 0.0;   // Will be computed on demand
}

// Calculate Doppler shift from satellite motion for specific satellite
struct DopplerData
{
    double radial_velocity;    // m/s (positive = approaching, negative = receding)
    double doppler_shift_hz;   // Hz at carrier frequency
    double doppler_ppm;        // Parts per million
};

DopplerData CalculateDoppler(const std::string& satName, double simTime, double carrier_freq_hz)
{
    DopplerData result = {0, 0, 0};
    
    // Get position at current time
    double x1, y1, z1, elev1, dist1;
    InterpolatePosition(satName, simTime, x1, y1, z1, elev1, dist1);
    
    // Get position at time + dt for velocity calculation
    double dt = 1.0;  // 1 second difference
    double x2, y2, z2, elev2, dist2;
    InterpolatePosition(satName, simTime + dt, x2, y2, z2, elev2, dist2);
    
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

UeSatMetrics UpdateSatelliteState(const std::string& satName, double simTime, int ueId)
{
    auto& ue = g_ues[ueId];
    double discreteTime = std::floor(simTime + 1e-6);

    auto cacheIt = ue.channelCache.find(satName);
    if (cacheIt != ue.channelCache.end() && cacheIt->second.first == discreteTime) {
        return cacheIt->second.second;
    }

    UeSatMetrics m = {-90.0, 1e9, 0.0, -999.0};
    auto it = g_satellites.find(satName);
    if (it == g_satellites.end() || it->second.trajectory.empty()) return m;

    double satStartTime = it->second.trajectory.front().time;
    double satEndTime = it->second.trajectory.back().time;
    if (simTime < satStartTime || simTime > satEndTime) {
        return m; 
    }

    double sat_x, sat_y, sat_z, _elev_unused, _dist_unused;
    InterpolatePosition(satName, simTime, sat_x, sat_y, sat_z, _elev_unused, _dist_unused);

    double dx = sat_x - ue.ecefX;
    double dy = sat_y - ue.ecefY;
    double dz = sat_z - ue.ecefZ;
    m.distance = std::sqrt(dx*dx + dy*dy + dz*dz);
    if (m.distance < 1.0) return m;

    double ue_r = std::sqrt(ue.ecefX*ue.ecefX + ue.ecefY*ue.ecefY + ue.ecefZ*ue.ecefZ);
    double dot_rn = dx*(ue.ecefX/ue_r) + dy*(ue.ecefY/ue_r) + dz*(ue.ecefZ/ue_r);
    m.elevation = std::asin(std::max(-1.0, std::min(1.0, dot_rn / m.distance))) * 180.0 / M_PI;

    const double dt = 1.0;
    double sx2, sy2, sz2, _e2, _d2;
    InterpolatePosition(satName, simTime + dt, sx2, sy2, sz2, _e2, _d2);
    double vx = (sx2 - sat_x) / dt;
    double vy = (sy2 - sat_y) / dt;
    double vz = (sz2 - sat_z) / dt;
    
    double rx = (ue.ecefX - sat_x) / m.distance;
    double ry = (ue.ecefY - sat_y) / m.distance;
    double rz = (ue.ecefZ - sat_z) / m.distance;
    double v_radial = vx*rx + vy*ry + vz*rz;
    m.dopplerShift = g_frequency * v_radial / SPEED_OF_LIGHT;

    double frequencyGHz = g_frequency / 1e9;
    double fspl = 32.45 + 20.0*std::log10(frequencyGHz) + 20.0*std::log10(m.distance);
    double atmosLoss = 0.2;
    double tropLoss = Get3GPPParameter(g_troposphericScintillation, m.elevation);
    
    if (ue.shadowFadingCache.find(satName) == ue.shadowFadingCache.end()) {
        std::normal_distribution<double> stdNormal(0.0, 1.0);
        ue.shadowFadingCache[satName] = stdNormal(ue.rng);
    }

    double sfFeature = ue.shadowFadingCache[satName];

    std::string s_lower = ue.scenario;
    if (!s_lower.empty() && s_lower.back() == '\r') s_lower.pop_back();
    std::transform(s_lower.begin(), s_lower.end(), s_lower.begin(), ::tolower);
    
    bool isUrban = (s_lower.find("urban") != std::string::npos && s_lower.find("suburban") == std::string::npos);
    
    double plos = 1.0;
    
    const std::vector<double> elevAngles = {10.0, 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0};
    std::vector<double> probTable;
    if (isUrban) {
        probTable = {0.246, 0.386, 0.493, 0.613, 0.726, 0.805, 0.919, 0.968, 0.992};
    } else {
        probTable = {0.782, 0.869, 0.919, 0.929, 0.935, 0.940, 0.949, 0.952, 0.998};
    }
    
    double clampedElev = std::max(10.0, std::min(90.0, m.elevation));
    size_t idx = 0;
    while (idx < elevAngles.size() - 1 && elevAngles[idx + 1] <= clampedElev) { idx++; }
    if (idx == elevAngles.size() - 1) {
        plos = probTable[idx];
    } else {
        double alpha = (clampedElev - elevAngles[idx]) / (elevAngles[idx + 1] - elevAngles[idx]);
        plos = probTable[idx] + alpha * (probTable[idx + 1] - probTable[idx]);
    }

    double clutterLoss = 0.0;
    double shadowStd = 0.0;
    bool isLOS;

    auto losIt = ue.losStateCache.find(satName);

    if (losIt == ue.losStateCache.end()) 
    {
        std::uniform_real_distribution<double> uniformDist(0.0, 1.0);
        isLOS = (uniformDist(ue.rng) <= plos);
        ue.losStateCache[satName] = isLOS;
        ue.lastEvalElev[satName] = m.elevation;
    }
    else
    {
        isLOS = losIt->second;
    }

    if (isLOS) {
        s_lower = isUrban ? "Urban-LOS" : "Rural-LOS";
        shadowStd = Get3GPPParameter(GetShadowFadingTable(s_lower), m.elevation);
    } else {
        s_lower = isUrban ? "Urban-NLOS" : "Rural-NLOS";
        clutterLoss = Get3GPPParameter(GetClutterLossTable(s_lower), m.elevation);
        shadowStd = Get3GPPParameter(GetShadowFadingTable_NLOS(s_lower), m.elevation);
    }

    double shadowFading = sfFeature * shadowStd;
    
    // Rician Fading
    double kFactorDb = GetRicianKFactorDb(s_lower, m.elevation);
    double K = std::pow(10.0, kFactorDb / 10.0);
    std::normal_distribution<double> gauss(0.0, 1.0);
    double sigma = std::sqrt(1.0 / (2 * (K + 1)));
    double s = std::sqrt(K / (K + 1));
    double x = sigma * gauss(ue.rng);
    double y = sigma * gauss(ue.rng);
    double r = std::sqrt(std::pow(s + x, 2) + std::pow(y, 2));
    double ricianFading = -20 * std::log10(r);

    double totalPathLoss = fspl + atmosLoss + tropLoss + clutterLoss + shadowFading + ricianFading;
    double rxPower = g_txPower + g_txAntennaGain + g_rxGain - totalPathLoss;
    double noisePowerDbm = -174.0 + 10.0*std::log10(g_bandwidth) + g_noiseFigure;

    double residualDoppler = std::abs(m.dopplerShift) * 0.01;
    double frequencyOffsetRatio = residualDoppler / g_bandwidth;
    double dopplerSnrLoss = (frequencyOffsetRatio > 0.001) ? 10.0 * std::log10(1.0 + frequencyOffsetRatio * 100.0) : 0.0;

    m.snrDb = rxPower - noisePowerDbm + g_fecCodingGain - dopplerSnrLoss;
    
    ue.channelCache[satName] = std::make_pair(simTime, m);

    return m;
}

// Select best satellite based on DISTANCE (primary) or ELEVATION (fallback)
// ==================================================================================
// UE-SIDE MEASUREMENT FUNCTIONS (3GPP TS 38.331 compliant)
// UE performs local measurements WITHOUT global/god-view access
// ==================================================================================

// ==================================================================================
// UE-SIDE MEASUREMENT FUNCTIONS (3GPP TS 38.331 compliant) - Multi-UE Version
// ==================================================================================

// UE performs radio measurements on serving and neighbor cells
MeasResultCell UE_MeasureCell(int ueId, const std::string& cellId, double simTime)
{
    MeasResultCell result;
    result.cellId = cellId;
    result.measurementTime = simTime;
    
    auto cacheIt = g_ues[ueId].channelCache.find(cellId);
    if (cacheIt == g_ues[ueId].channelCache.end())
    {
        result.rsrp = -150.0;  
        result.rsrq = -30.0;
        result.sinr = -20.0;
        result.distance = 1e9;
        result.elevation = -90.0;
        result.dopplerShift = 0.0;
        return result;
    }
    
    const UeSatMetrics& metrics = cacheIt->second.second;
    
    result.sinr = metrics.snrDb;
    double noiseFloor = -174.0 + 10*log10(g_bandwidth) + g_noiseFigure; // dBm
    result.rsrp = noiseFloor + result.sinr;
    result.rsrq = result.sinr - 3.0;  
    
    result.distance = metrics.distance;
    result.elevation = metrics.elevation;
    result.dopplerShift = metrics.dopplerShift;
    
    return result;
}

// UE performs periodic measurements on serving and neighbor cells
std::vector<MeasResultCell> UE_PerformMeasurements(int ueId, double simTime)
{
    std::vector<MeasResultCell> measurements;
    auto& ue = g_ues[ueId];
    

    for (const auto& pair : ue.channelCache)
    {
        const std::string& cellId = pair.first;
        const UeSatMetrics& metrics = pair.second.second;
        
  
        if (metrics.elevation < 0.0) continue;
        

        auto satIt = g_satellites.find(cellId);
        if (satIt != g_satellites.end() && !satIt->second.trajectory.empty()) {
            if (simTime > satIt->second.trajectory.back().time) continue;
        }
        
        if (metrics.snrDb < -6) continue; 
        
        MeasResultCell meas = UE_MeasureCell(ueId, cellId, simTime);
        measurements.push_back(meas);
    }
    
    return measurements;
}

bool UE_EvaluateCHOPreparationCondition(
    const MeasResultCell& serving, 
    const MeasResultCell& neighbor, 
    double reportingOffset)
{
    double servingDistKm = serving.distance / 1000.0;
    double targetDistKm  = neighbor.distance / 1000.0;

    return targetDistKm < servingDistKm + reportingOffset;
}

bool UE_EvaluateNtnTriggers(int ueId, const MeasResultCell& serving)
{
    return false;
}

void EvaluatePendingPrediction(PendingPredictionEval pendingEval);
void SchedulePendingPredictionEval(int ueId, const std::string& servingCell, const std::string& neighborCell,
                                   double decisionTime, double evalTime);
// UE decides whether to send MeasurementReport to serving gNB
// This implements the complete UE measurement and reporting logic
// Returns nullptr if no report needed, otherwise returns MeasurementReport
MeasurementReport* UE_EvaluateAndReport(int ueId, double simTime)
{
    auto& ue = g_ues[ueId];
    auto& measState = ue.measState;
    auto& measConfig = ue.measConfig;
    auto& hoContext = ue.hoContext;

    if (measState.servingCellId.empty())
    {
        ResetHoContext(hoContext);
        return nullptr;
    }
    
    std::vector<MeasResultCell> measurements = UE_PerformMeasurements(ueId, simTime);
    
    MeasResultCell* servingMeas = nullptr;
    for (auto& meas : measurements)
    {
        if (meas.cellId == measState.servingCellId) {
            servingMeas = &meas;
            break;
        }
    }
    
    if (!servingMeas)
    {
        ResetHoContext(hoContext);
        MeasurementReport* report = new MeasurementReport();
        report->reportTime = simTime;
        report->servingCell.cellId = measState.servingCellId;
        report->servingCell.measurementTime = simTime;
        report->servingCell.sinr = -999.0;
        report->eventType = "RLF";
        report->triggerReason = "Radio Link Failure - serving cell not detectable";
        report->servingCellBelowThreshold = true;
        report->betterNeighborAvailable = false;
        return report;
    }
    
    measState.lastMeasurements[measState.servingCellId] = *servingMeas;
    bool ntnTrigger = UE_EvaluateNtnTriggers(ueId, *servingMeas);
    
    MeasResultCell* bestNeighbor = nullptr;
    double bestDist = 1e18;
    
    for (auto& meas : measurements)
    {
        if (meas.cellId == measState.servingCellId) continue;
        
        measState.lastMeasurements[meas.cellId] = meas;
        if (meas.distance < bestDist)
        {
            bestDist = meas.distance;
            bestNeighbor = &meas;
        }
    }
    
    bool eventA3Triggered = false;
    if (bestNeighbor) {
        eventA3Triggered = UE_EvaluateCHOPreparationCondition(
            *servingMeas, *bestNeighbor, g_reportingOffset);

        std::cout << "[HO_DEBUG]"
                  << ",ue=" << ueId
                  << ",t=" << std::fixed << std::setprecision(3) << simTime
                  << ",serving=" << servingMeas->cellId
                  << ",neighbor=" << bestNeighbor->cellId
                  << ",serving_sinr=" << std::setprecision(2) << servingMeas->sinr
                  << ",neighbor_sinr=" << bestNeighbor->sinr
                  << ",serving_trend=" << 0.0
                  << ",neighbor_trend=" << 0.0
                  << ",serving_dist=" << servingMeas->distance
                  << ",neighbor_dist=" << bestNeighbor->distance
                  << ",neighbor_dist_trend=" << 0.0
                  << ",dynamic_margin=" << 0.0
                  << ",pred_serving=" << servingMeas->sinr
                  << ",pred_neighbor=" << bestNeighbor->sinr
                  << ",serving_trend=" << 0.0
                  << ",neighbor_trend=" << 0.0
                  << ",trend_diff=" << 0.0
                  << ",degradation_trigger=" << 0
                  << ",degradation_threshold=" << -1.0
                  << ",a3=" << (eventA3Triggered ? 1 : 0)
                  << ",prediction=" << 0
                  << std::endl;

        SchedulePendingPredictionEval(
            ueId,
            servingMeas->cellId,
            bestNeighbor->cellId,
            simTime,
            simTime + g_predictionEvalDelay);

        if (eventA3Triggered)
        {
            hoContext.preServingSnr = servingMeas->sinr;
            hoContext.preNeighborSnr = bestNeighbor->sinr;
            hoContext.decisionTime = simTime;
            hoContext.valid = true;
            hoContext.sourceCellId = servingMeas->cellId;
            hoContext.targetCellId = bestNeighbor->cellId;
            hoContext.triggerType = "A3";
        }
        else
        {
            ResetHoContext(hoContext);
        }
    }
    
    bool triggerConditionMet = ntnTrigger || eventA3Triggered;
    
    if (triggerConditionMet)
    {
        std::string candidateCell = bestNeighbor ? bestNeighbor->cellId : "";
        
        if (!measState.triggerActive)
        {
            measState.triggerActive = true;
            measState.triggerStartTime = simTime;
            measState.candidateTargetCell = candidateCell; 
            return nullptr;
        }
        else
        {
            if (!ntnTrigger && candidateCell != measState.candidateTargetCell)
            {
                measState.triggerStartTime = simTime;
                measState.candidateTargetCell = candidateCell;
                return nullptr;
            }
            
            double triggerDuration = simTime - measState.triggerStartTime;
            
            if (triggerDuration >= measConfig.timeToTrigger)
            {
                MeasurementReport* report = new MeasurementReport();
                report->reportTime = simTime;
                report->servingCell = *servingMeas;
                if (bestNeighbor) report->neighborCells.push_back(*bestNeighbor);
                
                if (ntnTrigger) {
                    report->eventType = "NTN_CRITICAL";
                    report->triggerReason = "NTN distance threshold exceeded";
                } else {
                    report->eventType = "CHO_PREP";
                    report->triggerReason = "CHO Phase-1 MR triggered";
                }
                
                report->servingCellBelowThreshold = ntnTrigger;
                report->betterNeighborAvailable = (bestNeighbor != nullptr);
                
                measState.reportsSent++;
                measState.lastReportTime = simTime;
                measState.triggerActive = false;
                
                return report;
            }
        }
    }
    else
    {
        measState.triggerActive = false;
        measState.triggerStartTime = -1.0;
        measState.candidateTargetCell = "";
        ResetHoContext(hoContext);
    }
    return nullptr;
}

// Helper: Check if a cell/link is viable (Liveness check during execution phase)
bool CHO_IsLinkViable(int ueId, const std::string& cellId, double currentTime)
{
    auto cacheIt = g_ues[ueId].channelCache.find(cellId);
    if (cacheIt == g_ues[ueId].channelCache.end()) return false;
    
    const UeSatMetrics& metrics = cacheIt->second.second;
    
    if (metrics.snrDb < g_choNtnConfig.minViableSinr) return false;
    if (metrics.distance / 1000.0 > g_choNtnConfig.maxViableDistance) return false;
    if (metrics.elevation < 0.0) return false;
    
    return true;
}

double GetCachedSnr(const UeInfo& ue, const std::string& cellId)
{
    auto it = ue.channelCache.find(cellId);
    if (it == ue.channelCache.end())
    {
        return -999.0;
    }
    return it->second.second.snrDb;
}

void EvaluatePendingPrediction(PendingPredictionEval pendingEval)
{
    g_pendingPredictionEvals.erase(
        std::remove_if(
            g_pendingPredictionEvals.begin(),
            g_pendingPredictionEvals.end(),
            [&pendingEval](const PendingPredictionEval& item) {
                return item.ueId == pendingEval.ueId &&
                       item.servingCell == pendingEval.servingCell &&
                       item.neighborCell == pendingEval.neighborCell &&
                       std::abs(item.decisionTime - pendingEval.decisionTime) < 1e-9;
            }),
        g_pendingPredictionEvals.end());

    if (pendingEval.ueId < 0 || pendingEval.ueId >= static_cast<int>(g_ues.size()))
    {
        return;
    }

    const auto& ue = g_ues[pendingEval.ueId];
    auto servingIt = ue.channelCache.find(pendingEval.servingCell);
    auto neighborIt = ue.channelCache.find(pendingEval.neighborCell);
    if (servingIt == ue.channelCache.end() || neighborIt == ue.channelCache.end())
    {
        return;
    }

    double currentTime = Simulator::Now().GetSeconds();
    double futureServingSnr = servingIt->second.second.snrDb;
    double futureNeighborSnr = neighborIt->second.second.snrDb;

    std::cout << "[FUTURE_DEBUG]"
              << ",ue=" << pendingEval.ueId
              << ",t_decision=" << std::fixed << std::setprecision(3) << pendingEval.decisionTime
              << ",t_eval=" << currentTime
              << ",future_serving_sinr=" << std::setprecision(2) << futureServingSnr
              << ",future_neighbor_sinr=" << futureNeighborSnr
              << std::endl;
}

void SchedulePendingPredictionEval(int ueId, const std::string& servingCell, const std::string& neighborCell,
                                   double decisionTime, double evalTime)
{
    PendingPredictionEval pendingEval{ueId, servingCell, neighborCell, decisionTime, evalTime};
    g_pendingPredictionEvals.push_back(pendingEval);

    double delay = std::max(0.0, evalTime - Simulator::Now().GetSeconds());
    Simulator::Schedule(Seconds(delay), &EvaluatePendingPrediction, pendingEval);
}
// ==================================================================================
// Forward declarations 
// ==================================================================================
void CHO_Phase1_ReportArrived(int ueId, std::string sourceCell, std::string targetCell);
void CHO_Phase2_SendXnRequest(int ueId, std::string sourceCell, std::string targetCell);
void CHO_Phase3_XnResponse(int ueId, std::string sourceCell, std::string targetCell);
void CHO_Phase4_SendRrcReconfig(int ueId, std::string sourceCell, std::string targetCell);
void CHO_Phase5_UeStoresConfig(int ueId, std::string sourceCell, std::string targetCell);
void CHO_Phase6_UeExecutes(int ueId, std::string sourceCell, std::string targetCell);
void CHO_Phase7_ExecutionComplete(int ueId, std::string sourceCell, std::string targetCell);
void CHO_HandleFailure(int ueId, std::string reason, double failureTime);
void UpdateChannelState();           
void CheckHandoverConditions();      


double GetUuDelayForUe(int ueId, const std::string& satName) {
    auto it = g_ues[ueId].channelCache.find(satName);
    if (it != g_ues[ueId].channelCache.end()) {
        return it->second.second.distance / SPEED_OF_LIGHT;
    }
    return 0.010; // Default 10ms fallback value
}

// Immediately update the ErrorModel for a specific UE (for precise interruption at handover moment)
void ApplyUeErrorModelImmediately(int ueId) {
    auto& ue = g_ues[ueId];
    std::string activeSat = ue.context.servingGnb;
    
    if (activeSat.empty() || ue.channelCache.count(activeSat) == 0) return;
    
    const UeSatMetrics& metrics = ue.channelCache[activeSat].second;
    double snrLinear = std::pow(10.0, metrics.snrDb / 10.0);
    double ber = 0.5 * std::erfc(std::sqrt(snrLinear));
    double per = 1.0 - std::pow(1.0 - ber, g_packetSize * 8);
    if (per < 0.0) per = 0.0;
    if (per > 1.0) per = 1.0;
    
    // Force link disconnection judgment
    if (ue.hoState.interruptionActive) per = 1.0; 
    
    if (ue.ulErrorModel) ue.ulErrorModel->SetAttribute("ErrorRate", DoubleValue(per));
    if (ue.dlErrorModel) ue.dlErrorModel->SetAttribute("ErrorRate", DoubleValue(per));
}

// ==================================================================================
// CHO Procedure Implementation (Multi-UE Version)
// ==================================================================================  

// Trigger CHO procedure when measurement report is generated
void CHO_TriggerProcedure(int ueId, const MeasurementReport* report)
{
    auto& choState = g_ues[ueId].choProcState;

    if (!report || choState.phase != CHO_IDLE) return;
    
    double currentTime = Simulator::Now().GetSeconds();
    
    choState.phase = CHO_MEAS_REPORT_UPLINK;
    choState.sourceCell = report->servingCell.cellId;
    choState.targetCell = report->neighborCells.empty() ? "" : report->neighborCells[0].cellId;
    choState.procedureStartTime = currentTime;
    choState.phaseStartTime = currentTime;
    choState.failed = false;
    choState.failureReason = "";
    
    choState.servingSnrAtReport = report->servingCell.sinr;
    choState.targetSnrAtReport = report->neighborCells.empty() ? -999.0 : report->neighborCells[0].sinr;
    
    choState.measReportUplinkDelay = GetUuDelayForUe(ueId, choState.sourceCell) + DELAY_RRC_PROC;
    choState.reconfigDownlinkDelay = GetUuDelayForUe(ueId, choState.sourceCell) + DELAY_RRC_PROC;
   
    g_choNtnStats.totalReports++;
    g_choNtnStats.lastReportTime = currentTime;
    
    double sourceDist = report->servingCell.distance / 1000.0;
    double targetDist = report->neighborCells.empty() ? 0.0 : report->neighborCells[0].distance / 1000.0;
    
    std::cout << "\n➤ [UE-" << ueId << "] CHO PROCEDURE INITIATED at t=" 
              << std::fixed << std::setprecision(1) << currentTime << "s" << std::endl;
    std::cout << "  Source: " << choState.sourceCell 
              << " (SINR=" << std::setprecision(2) << report->servingCell.sinr << " dB, "
              << "Dist=" << std::setprecision(0) << sourceDist << " km)" << std::endl;
    std::cout << "  Target: " << choState.targetCell 
              << " (SINR=" << std::setprecision(2) << choState.targetSnrAtReport << " dB, "
              << "Dist=" << std::setprecision(0) << targetDist << " km)" << std::endl;
    
    // Log
    g_choLog << currentTime << "," << ueId << ",CHO_TRIGGERED,"
             << choState.sourceCell << "," << choState.targetCell << ","
             << report->servingCell.sinr << "," << choState.targetSnrAtReport << ","
             << (choState.measReportUplinkDelay * 1000.0) << ",,START" << std::endl;
             
    g_xnSignalingLog << currentTime << "," << ueId << ","
                     << "MEAS_REPORT_UPLINK,"
                     << "UE," << choState.sourceCell << ","
                     << "Uu,"
                     << "MeasReport_delay=" << (choState.measReportUplinkDelay * 1000.0) << "ms" << std::endl;
    
                     // Schedule Phase 1
    Simulator::Schedule(Seconds(choState.measReportUplinkDelay),
                       &CHO_Phase1_ReportArrived, ueId,
                       choState.sourceCell, choState.targetCell);
}

void CHO_Phase1_ReportArrived(int ueId, std::string sourceCell, std::string targetCell)
{
    double currentTime = Simulator::Now().GetSeconds();
    auto& choState = g_ues[ueId].choProcState;
    
    if (choState.phase != CHO_MEAS_REPORT_UPLINK) return;
    
    std::cout << "  [UE-" << ueId << "] Step 1: gNB RECEIVED MEASUREMENT REPORT at t=" 
              << std::setprecision(1) << currentTime << "s" << std::endl;
    
    g_xnSignalingLog << currentTime << "," << ueId << ","
                     << "MEAS_REPORT_RECEIVED,"
                     << sourceCell << "," << "gNB," << "Uu,"
                     << "Report_processed" << std::endl;

    // Step 2: Source gNB DECISION
    choState.phase = CHO_GNB_CHO_DECISION;
    choState.phaseStartTime = currentTime;
    
    std::cout << "  [UE-" << ueId << "] Step 2: gNB DECISION → Use CONDITIONAL HANDOVER" << std::endl;
    
    // Proceed to Step 3: Send CHO Request over Xn
    Simulator::Schedule(Seconds(0.001), &CHO_Phase2_SendXnRequest, ueId, sourceCell, targetCell);
}

void CHO_Phase2_SendXnRequest(int ueId, std::string sourceCell, std::string targetCell)
{
    double currentTime = Simulator::Now().GetSeconds();
    auto& choState = g_ues[ueId].choProcState;
    
    if (choState.phase != CHO_GNB_CHO_DECISION) return;
    
    choState.phase = CHO_XN_REQUEST;
    choState.phaseStartTime = currentTime;
    
    std::cout << "  [UE-" << ueId << "] Step 3: CHO REQUEST via Xn to candidate gNB(s)" << std::endl;
    
    double islDelay = GetIslDelay(sourceCell, targetCell, currentTime);

    // Log Xn signaling
    g_xnSignalingLog << currentTime << "," << ueId << ","
                     << "CHO_REQUEST,"
                     << sourceCell << "," << targetCell << ","
                     << "Xn," << "CHO_indication=true,ISL_delay=" 
                     << (islDelay * 1000) << "ms" << std::endl;
    
                     // Schedule Step 5
    Simulator::Schedule(Seconds(islDelay + 0.002),
                       &CHO_Phase3_XnResponse, ueId,
                       sourceCell, targetCell);
}

void CHO_Phase3_XnResponse(int ueId, std::string sourceCell, std::string targetCell)
{
    double currentTime = Simulator::Now().GetSeconds();
    auto& choState = g_ues[ueId].choProcState;
    
    if (choState.phase != CHO_XN_REQUEST) return;
    
    choState.phase = CHO_XN_RESPONSE;
    choState.phaseStartTime = currentTime;
    
    std::cout << "  [UE-" << ueId << "] Step 5: CHO RESPONSE from candidate gNB" << std::endl;
    
    double islDelay = GetIslDelay(targetCell, sourceCell, currentTime);
    
    // Log Xn signaling
   g_xnSignalingLog << currentTime << "," << ueId << ","
                     << "CHO_RESPONSE,"
                     << targetCell << "," << sourceCell << ","
                     << "Xn," << "Admit_UE,CHO_config,ISL_delay=" 
                     << (islDelay * 1000) << "ms" << std::endl;

    Simulator::Schedule(Seconds(islDelay + 0.001),
                       &CHO_Phase4_SendRrcReconfig, ueId,
                       sourceCell, targetCell);
}

void CHO_Phase4_SendRrcReconfig(int ueId, std::string sourceCell, std::string targetCell)
{
    double currentTime = Simulator::Now().GetSeconds();
    auto& choState = g_ues[ueId].choProcState;
    
    if (choState.phase != CHO_XN_RESPONSE) return;
    
    choState.phase = CHO_RRC_RECONFIG_DOWNLINK;
    choState.phaseStartTime = currentTime;
    
    std::cout << "  [UE-" << ueId << "] Step 6: RRCReconfiguration (CHO Config) sent" << std::endl;
    g_choNtnStats.configsSent++;

    // Log Xn signaling 
    g_xnSignalingLog << currentTime << "," << ueId << ","
                     << "RRC_RECONFIGURATION,"
                     << sourceCell << "," << "UE," << "Uu,"
                     << "CHO_config={target=" << targetCell << "},delay=" 
                     << (choState.reconfigDownlinkDelay * 1000.0) 
                     << "ms" << std::endl;

    Simulator::Schedule(Seconds(choState.reconfigDownlinkDelay),
                       &CHO_Phase5_UeStoresConfig, ueId,
                       sourceCell, targetCell);
}

void CHO_Phase5_UeStoresConfig(int ueId, std::string sourceCell, std::string targetCell)
{
    double currentTime = Simulator::Now().GetSeconds();
    auto& choState = g_ues[ueId].choProcState;
    auto& choConfigState = g_ues[ueId].choConfigState;
    
    if (choState.phase != CHO_RRC_RECONFIG_DOWNLINK) return;
    
    choState.phase = CHO_CONFIG_STORED;
    choState.phaseStartTime = currentTime;
    choState.configReceivedTime = currentTime;
    choState.configExpiryTime = currentTime + g_choNtnConfig.configValidityPeriod;
    
    g_choNtnStats.configsReceived++;
    
    std::cout << "  [UE-" << ueId << "] Step 7: UE STORED CHO CONFIG (Validity: " 
              << g_choNtnConfig.configValidityPeriod << "s)" << std::endl;
    std::cout << "  [UE-" << ueId << "] Step 8: UE now AUTONOMOUSLY evaluating execution condition..." << std::endl;
    
    double ackUplinkDelay = GetUuDelayForUe(ueId, sourceCell);

    // Log Xn signaling: RRCReconfigurationComplete (UE→Source via Uu)
    g_choLog << currentTime << "," << ueId << ",CONFIG_STORED,"
             << sourceCell << "," << targetCell << ",,,,,STORED_ACK" 
             << std::endl;

    g_xnSignalingLog << (currentTime + ackUplinkDelay) << "," << ueId << ","
                     << "RRC_RECONFIG_COMPLETE,"
                     << "UE," << sourceCell << "," << "Uu,"
                     << "CHO_config_acknowledged" << std::endl;

    // Transition to autonomous evaluation phase
    choState.phase = CHO_UE_EVALUATING;
    choState.executionTttActive = false;
    choState.executionTttStartTime = -1.0;
    choState.executionTimeToTrigger = g_choNtnConfig.executionTimeToTrigger;
    
    // Store CHO config in UE state
    ChoConfiguration choConfig;
    choConfig.targetCellId = targetCell;
    choConfig.configurationTime = currentTime;
    choConfig.isValid = true;
    choConfig.expiryTime = choState.configExpiryTime;
    
    choConfigState.choConfigs[targetCell] = choConfig;
    choConfigState.choConfigsReceived++;

    // Log Xn signaling: Conditional Config received and stored 
    g_xnSignalingLog << currentTime << "," << ueId << ","
                     << "CONDITIONAL_CONFIG_STORED,"
                     << "UE," << sourceCell << "," << "Uu,"
                     << "Config_valid_until=" 
                     << choState.configExpiryTime << "s" << std::endl;
}

void CHO_Phase6_UeExecutes(int ueId, std::string sourceCell, std::string targetCell)
{
    double currentTime = Simulator::Now().GetSeconds();
    auto& choState = g_ues[ueId].choProcState;
    auto& choConfigState = g_ues[ueId].choConfigState;
    auto& hoState = g_ues[ueId].hoState;
    
    if (choState.phase != CHO_UE_EVALUATING) return;
    
    choConfigState.executionPending = true;
    choConfigState.executionTargetCell = targetCell;
    choConfigState.executionTriggerTime = currentTime;
    
    choState.phase = CHO_UE_EXECUTING;
    choState.phaseStartTime = currentTime;
    choState.executionStartTime = currentTime;
    
    // Record SNRs at execution trigger from Cache
    auto servingIt = g_ues[ueId].channelCache.find(sourceCell);
    if (servingIt != g_ues[ueId].channelCache.end()) {
        choState.servingSnrAtExecution = servingIt->second.second.snrDb;
    }
    auto targetIt = g_ues[ueId].channelCache.find(targetCell);
    if (targetIt != g_ues[ueId].channelCache.end()) {
        choState.targetSnrAtExecution = targetIt->second.second.snrDb;
    }
    
    g_choNtnStats.executionsTriggered++;
    
    double uuDelaySource = GetUuDelayForUe(ueId, sourceCell);
    double uuDelayTarget = GetUuDelayForUe(ueId, targetCell);
    double ueProcessing = DELAY_RRC_PROC + DELAY_BEAM_SWITCH; 
    double rachDuration = (uuDelayTarget * 4.0) + 0.020; 
    double totalExecutionTime = uuDelaySource + ueProcessing + rachDuration;
    
    std::cout << "  [UE-" << ueId << "] UE AUTONOMOUSLY TRIGGERS EXECUTION" << std::endl;
    std::cout << "  → Total execution delay: " << std::setprecision(1) << (totalExecutionTime * 1000.0) << " ms" << std::endl;
    
    g_choLog << currentTime << "," << ueId << ",EXECUTION_TRIGGERED,"
             << sourceCell << "," << targetCell << ","
             << choState.servingSnrAtExecution << "," 
             << choState.targetSnrAtExecution << ","
             << (totalExecutionTime * 1000.0) << ",,AUTONOMOUS" << std::endl;
             
    g_xnSignalingLog << currentTime << "," << ueId << ","
                     << "CHO_AUTONOMOUS_EXECUTION,"
                     << "UE," << targetCell << "," << "Uu,"
                     << "UE_autonomous_trigger,exec_delay=" 
                     << (totalExecutionTime * 1000.0) << "ms" << std::endl;
    
    hoState.interruptionActive = true;
    ApplyUeErrorModelImmediately(ueId);
    
    Simulator::Schedule(Seconds(totalExecutionTime),
                       &CHO_Phase7_ExecutionComplete, ueId,
                       sourceCell, targetCell);
}

void CHO_Phase7_ExecutionComplete(int ueId, std::string sourceCell, std::string targetCell)
{
    double currentTime = Simulator::Now().GetSeconds();
    auto& ue = g_ues[ueId];
    
    if (ue.choProcState.phase != CHO_UE_EXECUTING) return;
    
    std::cout << "  [UE-" << ueId << "] Phase 7: EXECUTION COMPLETE at t=" 
              << std::setprecision(1) << currentTime << "s" << std::endl;
    std::cout << "  → LIVENESS CHECK: Verifying serving and target links..." << std::endl;
    
    bool servingViable = CHO_IsLinkViable(ueId, sourceCell, currentTime);
    bool targetViable = CHO_IsLinkViable(ueId, targetCell, currentTime);
    
    double servingSnr = -999.0, targetSnr = -999.0;
    auto servingIt = ue.channelCache.find(sourceCell);
    if (servingIt != ue.channelCache.end()) servingSnr = servingIt->second.second.snrDb;
    
    auto targetIt = ue.channelCache.find(targetCell);
    if (targetIt != ue.channelCache.end()) targetSnr = targetIt->second.second.snrDb;
    
    std::cout << "    Serving SINR: " << std::setprecision(2) << servingSnr 
              << " dB (" << (servingViable ? "viable" : "NOT viable") << ")" << std::endl;
    std::cout << "    Target SINR: " << std::setprecision(2) << targetSnr 
              << " dB (" << (targetViable ? "viable" : "NOT viable") << ")" << std::endl;
    
    if (!targetViable)
    {
        std::string reason = "TARGET_NOT_VIABLE (SINR=" + std::to_string(targetSnr) + " dB)";
        std::cout << "    ✗ [UE-" << ueId << "] LIVENESS CHECK FAILED: " << reason << std::endl;
        g_choNtnStats.failedAtExecution++;
        CHO_HandleFailure(ueId, reason, currentTime);
        return;
    }
    
    std::cout << "    ✓ [UE-" << ueId << "] LIVENESS CHECK PASSED" << std::endl;
    
    // SUCCESS: Complete handover
    double totalProcedureDelay = currentTime - ue.choProcState.configReceivedTime;
    double executionOnlyDelay = currentTime - ue.choProcState.executionStartTime;
    
    g_choNtnStats.totalProcedureDelay += totalProcedureDelay;
    g_choNtnStats.totalExecutionDelay += executionOnlyDelay;
    g_choNtnStats.successfulHandovers++;
    g_choNtnStats.avgProcedureDelay = g_choNtnStats.totalProcedureDelay / g_choNtnStats.successfulHandovers;
    g_choNtnStats.avgExecutionDelay = g_choNtnStats.totalExecutionDelay / g_choNtnStats.successfulHandovers;
    
    ue.choProcState.phase = CHO_XN_CANCEL;
    
    // ==================================================================================
    // Step 8a: HANDOVER SUCCESS (Target→Source via Xn)
    // Target gNB informs source that UE has successfully synchronized
    // ==================================================================================
    double islDelayToSource = GetIslDelay(targetCell, sourceCell, currentTime);
    
    // Log Xn signaling: HANDOVER SUCCESS
   g_xnSignalingLog << currentTime << "," << ueId << ","
                     << "HANDOVER_SUCCESS,"
                     << targetCell << "," << sourceCell << "," << "Xn,"
                     << "CHO_execution_confirmed,ISL_delay=" 
                     << (islDelayToSource * 1000) << "ms" << std::endl;
   
    // ==================================================================================
    // Step 8b: SN STATUS TRANSFER (Source→Target via Xn)
    // Source forwards PDCP sequence numbers to target for data continuity
    // ==================================================================================
    double islDelayToTarget = GetIslDelay(sourceCell, targetCell, currentTime);
    
    g_xnSignalingLog << (currentTime + 0.002) << "," << ueId << ","
                     << "SN_STATUS_TRANSFER,"
                     << sourceCell << "," << targetCell << "," << "Xn,"
                     << "PDCP_SN_transfer,ISL_delay=" 
                     << (islDelayToTarget * 1000) << "ms" << std::endl;
    
    // ==================================================================================
    // Step 8c: HANDOVER CANCEL to other candidate gNBs (if any)
    // Source gNB cancels CHO preparations for candidates not executed by UE
    // ==================================================================================
    for (const auto& configPair : ue.choConfigState.choConfigs)
    {
        std::string candidateCell = configPair.first;
        if (candidateCell != targetCell)  // Don't cancel the executed one
        {
            double islDelayToCandidate = GetIslDelay(sourceCell, candidateCell, currentTime);
            
            g_xnSignalingLog << (currentTime + 0.003) << "," << ueId << ","
                             << "HANDOVER_CANCEL,"
                             << sourceCell << "," << candidateCell << "," 
                             << "Xn,"
                             << "CHO_not_executed,release_resources,ISL_delay=" 
                             << (islDelayToCandidate * 1000) << "ms" 
                             << std::endl;
            
            std::cout << "  [UE-" << ueId << "] Step 8c: HANDOVER CANCEL sent to candidate " << candidateCell << std::endl;
        }
    }
    
    // Update global and per-UE handover counter
    g_handoverCount++;
    ue.hoCount++;
    
    // Thoroughly update the UE's context
    ue.context.servingGnb = targetCell;
    ue.measState.servingCellId = targetCell;
    ue.measState.triggerActive = false;
    ue.measState.triggerStartTime = -1.0;
    ue.measState.candidateTargetCell = "";
    ue.measState.lastMeasurements.clear();
    
    // Clear CHO configs and pending state
    ue.choConfigState.executionPending = false;
    ue.choConfigState.executionTargetCell = "";
    ue.choConfigState.choConfigs.clear();
    
    ue.hoState.interruptionActive = false; 
    ApplyUeErrorModelImmediately(ueId);

    double snrImprovement = targetSnr - ue.choProcState.servingSnrAtReport;
    if (ue.hoContext.valid &&
        ue.hoContext.sourceCellId == sourceCell &&
        ue.hoContext.targetCellId == targetCell)
    {
        double postServingSnr = targetSnr;
        double sinrGain = postServingSnr - ue.hoContext.preServingSnr;
        double decisionDelay = currentTime - ue.hoContext.decisionTime;

        std::cout << "[HO_RESULT],ue=" << ueId
                  << ",t=" << std::fixed << std::setprecision(3) << currentTime
                  << ",pre_serving_sinr=" << std::setprecision(2) << ue.hoContext.preServingSnr
                  << ",pre_neighbor_sinr=" << ue.hoContext.preNeighborSnr
                  << ",post_serving_sinr=" << postServingSnr
                  << ",sinr_gain=" << sinrGain
                  << ",decision_delay=" << std::setprecision(3) << decisionDelay;
        if (!ue.hoContext.triggerType.empty())
        {
            std::cout << ",trigger_type=" << ue.hoContext.triggerType;
        }
        std::cout << std::endl;
    }
    ResetHoContext(ue.hoContext);

    std::cout << "✓ [UE-" << ueId << "] CHO HANDOVER COMPLETE: " << sourceCell << " → " << targetCell << std::endl;
    std::cout << "  Total Procedure Delay: " << std::setprecision(1) << (totalProcedureDelay * 1000.0) << " ms" << std::endl;
    std::cout << "  Execution-Only Delay: " << std::setprecision(1) << (executionOnlyDelay * 1000.0) << " ms" << std::endl;
    std::cout << "  SINR Improvement: " << std::setprecision(2) << snrImprovement << " dB" << std::endl;
    std::cout << "  Success Rate: " << std::setprecision(1) 
              << (100.0 * g_choNtnStats.successfulHandovers / g_choNtnStats.totalReports) << "%" << std::endl;
    std::cout << std::endl;
    
    // Log success
    g_choLog << currentTime << ",CHO_SUCCESS_UE" << ueId << ","
             << sourceCell << "," << targetCell << ","
             << servingSnr << "," << targetSnr << ","
             << (totalProcedureDelay * 1000.0) << "," << snrImprovement << ",COMPLETE" << std::endl;
    
    double oldDistKm = (servingIt != ue.channelCache.end()) ? servingIt->second.second.distance / 1000.0 : 0.0;
    double newDistKm = (targetIt != ue.channelCache.end()) ? targetIt->second.second.distance / 1000.0 : 0.0;

    g_handoverLog << currentTime << "," << ueId << "," 
                  << sourceCell << "," << targetCell << ","
                  << ue.choProcState.servingSnrAtReport << "," 
                  << targetSnr << ",0"
                  << "," << oldDistKm << "," << newDistKm << std::endl;
    
    // Log Xn signaling: CHO Complete and Path Switch
    g_xnSignalingLog << currentTime << "," << ueId << ","
                     << "CHO_COMPLETE,"
                     << targetCell << ","
                     << "5GC-AMF,"
                     << "NG,"
                     << "Path_switched,total_delay=" 
                     << (totalProcedureDelay * 1000.0) 
                     << "ms,exec_delay=" << (executionOnlyDelay * 1000.0) 
                     << "ms,UE=" << ueId << std::endl;
    
    // Reset CHO state
    ue.choProcState.phase = CHO_IDLE;
    ue.choProcState.failed = false;
}

void CHO_HandleFailure(int ueId, std::string reason, double failureTime)
{
    auto& ue = g_ues[ueId];
    double totalDelay = failureTime - ue.choProcState.procedureStartTime;
    
    std::cout << "✗ [UE-" << ueId << "] CHO HANDOVER FAILED: " << reason << std::endl;
    ResetHoContext(ue.hoContext);
    
    g_choLog << failureTime << "," << ueId << ",CHO_FAILURE,"
             << ue.choProcState.sourceCell 
             << "," << ue.choProcState.targetCell << ",,,,"
             << (totalDelay * 1000.0) << "," << reason << std::endl;
    
    g_handoverLog << failureTime << "," << ueId << "," 
                  << ue.choProcState.sourceCell << ","
                  << ue.choProcState.targetCell 
                  << ",,,FAILURE_" << reason << std::endl;

    ue.hoFail++;
    ue.hoState.interruptionActive = false;
    ue.choConfigState.choConfigs.clear();
    ue.choProcState.phase = CHO_IDLE;
    ue.choProcState.failed = true;
    ue.choProcState.failureReason = reason;
}

void CHO_CheckExecutionCondition(int ueId, double currentTime)
{
    auto& choState = g_ues[ueId].choProcState;
    auto& choConfigState = g_ues[ueId].choConfigState;
    
    if (choState.phase != CHO_UE_EVALUATING) return;
    
    if (currentTime > choState.configExpiryTime)
    {
        std::cout << "⚠ [UE-" << ueId << "] CHO config expired without execution" << std::endl;
        g_choNtnStats.configsExpired++;
        choState.phase = CHO_IDLE;
        choConfigState.choConfigs.clear();
        return;
    }
    
    std::string servingCell = choState.sourceCell;
    std::string targetCell = choState.targetCell;
    
    // Fetch the current SNR from this UE's dedicated Channel Cache
    auto servingIt = g_ues[ueId].channelCache.find(servingCell);
    auto targetIt = g_ues[ueId].channelCache.find(targetCell);
    
    if (servingIt == g_ues[ueId].channelCache.end() || targetIt == g_ues[ueId].channelCache.end()) return;
    
    double servingDistKm = servingIt->second.second.distance / 1000.0;
    double targetDistKm  = targetIt->second.second.distance / 1000.0;
    double distImprovement = servingDistKm - targetDistKm;

    // Phase-2 strict threshold: Target SNR must be greater than Serving SNR + Hysteresis (e.g., 3dB)
    bool conditionMet = (distImprovement >= g_choExecutionOffset) && ( (servingIt->second.second.distance / 1000.0) < g_maxDistance);
    
    if (conditionMet)
    {
        if (!choState.executionTttActive)
        {
            choState.executionTttActive = true;
            choState.executionTttStartTime = currentTime;
            std::cout << "  → [UE-" << ueId << "] CHO Phase-2 Execution Condition MET – Starting TTT_exec" << std::endl;
            return;
        }
        else
        {
            double tttDuration = currentTime - choState.executionTttStartTime;
            if (tttDuration >= choState.executionTimeToTrigger)
            {
                std::cout << "  → [UE-" << ueId << "] CHO Phase-2 TTT_exec EXPIRED – UE autonomous execution" << std::endl;
                choState.executionTttActive = false;
                CHO_Phase6_UeExecutes(ueId, servingCell, targetCell);
            }
        }
    }
    else
    {
        if (choState.executionTttActive) {
            std::cout << "  → [UE-" << ueId << "] Execution Condition NO LONGER MET – Resetting TTT_exec" << std::endl;
        }
        choState.executionTttActive = false;
        choState.executionTttStartTime = -1.0;
    }
}

/// ==================================================================================
// Initial Satellite Selection & Handover Triggers (Multi-UE Version)
// ==================================================================================

// Select best satellite based on SNR with hysteresis for a specific UE
std::string SelectBestSatellite(int ueId, const std::string& currentSat)
{
    std::string bestSat = currentSat;
    double bestDist = 1e18;
    double simTime = Simulator::Now().GetSeconds();
    auto& ue = g_ues[ueId];
    
    // Find current satellite's SNR from UE's cache
    double currentDist = 1e18;
    auto currentIt = ue.channelCache.find(currentSat);
    if (currentIt != ue.channelCache.end())
    {
        currentDist = currentIt->second.second.distance;
    }
    
    // Find satellite with best SNR from UE's perspective
    for (const auto& pair : ue.channelCache)
    {
        const std::string& satName = pair.first;
        const UeSatMetrics& metrics = pair.second.second;
        
        // VISIBILITY FILTER 1: Skip satellites below horizon
        if (metrics.elevation < 0.0) continue;
        
        // VISIBILITY FILTER 2: Skip expired trajectory
        auto satIt = g_satellites.find(satName);
        if (satIt != g_satellites.end() && !satIt->second.trajectory.empty() && simTime > satIt->second.trajectory.back().time)
        {
            continue;
        }

        if ( (metrics.distance / 1000.0 )> g_maxDistance)
        {
            // std::cout << "distance is : " << (dist / 1000.0) << std::endl;
            continue;
        }

        if ( metrics.snrDb < -6)
        {
            // std::cout << "distance is : " << (dist / 1000.0) << std::endl;
            continue;
        }

        if (metrics.distance < bestDist)
        {
            bestDist = metrics.distance;
            bestSat = satName;
            std::cout << "    → New best candidate: " << bestSat 
                    << " (Distance=" << std::setprecision(1) << bestDist/1000 << " km)" << std::endl;
        }
    }
    
    // Apply distance hysteresis: only switch if new satellite is significantly better
    if (bestSat != currentSat)

    {   double bestDistKm    = bestDist / 1000.0;
        double currentDistKm = currentDist / 1000.0;
        if (currentDistKm - bestDistKm < g_choExecutionOffset)
        {
            // New satellite not close enough, stay with current
            return currentSat;
        }
    }
    return bestSat;
}

// Check handover conditions for a specific UE
void CheckHandover(double simTime, int ueId)
{
    auto& ue = g_ues[ueId];

    // Skip if CHO procedure already in progress
    if (ue.choProcState.phase != CHO_IDLE)
    {
        if (ue.choProcState.phase == CHO_UE_EVALUATING)
        {
            CHO_CheckExecutionCondition(ueId, simTime);
        }
        return;
    }
    
    // Skip if legacy BHO in progress
    if (ue.hoState.currentPhase != PHASE_IDLE) return;
    
    // Get the currently serving cell for this UE
    std::string activeSat = ue.context.servingGnb;
    
    if (activeSat.empty()) return; // No active satellite

    // Update UE measurement state
    if (ue.measState.servingCellId.empty() || ue.measState.servingCellId != activeSat)
    {
        ue.measState.servingCellId = activeSat;
        ue.measState.lastMeasurementTime = simTime;
        ue.measState.triggerActive = false;
        ue.measState.triggerStartTime = -1.0;
        ue.measState.candidateTargetCell = "";
    }
    
    // Check if measurement period elapsed
    if (simTime - ue.measState.lastMeasurementTime < ue.measConfig.measurementPeriod) return;
    
    ue.measState.lastMeasurementTime = simTime;
    
    // STEP 1: UE performs measurements
    MeasurementReport* report = UE_EvaluateAndReport(ueId, simTime);
    
    // STEP 2: Trigger CHO procedure if report generated
   if (report)
    {
        auto& ue = g_ues[ueId];

        if (report->eventType == "RLF")
        {
            std::cout << "\n[t=" << std::fixed << std::setprecision(1) << simTime 
                      << "s] 🚨 [UE-" << ueId << "] RADIO LINK FAILURE! Serving cell lost. Forcing re-selection..." << std::endl;
            
            ue.context.servingGnb = "";       
            ue.measState.servingCellId = "";  
            ue.measState.triggerActive = false;
        }
        else if (!report->neighborCells.empty())
        {
            std::cout << "\n📤 [UE-" << ueId << "] GENERATES MeasurementReport" << std::endl;
            CHO_TriggerProcedure(ueId, report);
        }
        delete report;
    }
}

// ==================================================================================
// Master Periodic Loop for Channel Updates & Handover Evaluation
// ==================================================================================

// Print overall simulation link quality
void PrintLinkQuality(const std::string& context)
{
    if (!g_flowMonitor) return;
    
    double simTime = Simulator::Now().GetSeconds();
    g_flowMonitor->CheckForLostPackets();
    std::map<FlowId, FlowMonitor::FlowStats> stats = g_flowMonitor->GetFlowStats();
    
    uint32_t totalTx = 0, totalRx = 0, totalLost = 0;
    double avgDelay = 0;
    int flowCount = 0;
    
    for (auto& stat : stats) {
        totalTx += stat.second.txPackets;
        totalRx += stat.second.rxPackets;
        totalLost += stat.second.lostPackets;
        if (stat.second.rxPackets > 0) {
            avgDelay += stat.second.delaySum.GetMilliSeconds() / stat.second.rxPackets;
            flowCount++;
        }
    }
    if (flowCount > 0) avgDelay /= flowCount;
    
    double pdr = totalTx > 0 ? (double)totalRx / totalTx * 100.0 : 0.0;
    
    uint32_t recentTx = (totalTx > g_lastTxPackets) ? (totalTx - g_lastTxPackets) : 0;
    uint32_t recentRx = (totalRx > g_lastRxPackets) ? (totalRx - g_lastRxPackets) : 0;
    int32_t recentLost = static_cast<int32_t>(recentTx) - static_cast<int32_t>(recentRx);
    if (recentLost < 0) recentLost = 0;
    
    std::cout << "  📊 " << context << " Global Link Quality [t=" << simTime << "s]:" << std::endl;
    std::cout << "     Packets: Tx=" << totalTx << ", Rx=" << totalRx << ", Lost=" << totalLost;
    if (recentLost > 0) std::cout << " (Recent: +" << recentLost << ")";
    std::cout << "\n     PDR: " << std::fixed << std::setprecision(2) << pdr << "%"
              << ", Delay: " << avgDelay << " ms" << std::endl;
    
    g_lastTxPackets = totalTx;
    g_lastRxPackets = totalRx;
}

void UpdateChannelState()
{
    double simTime = Simulator::Now().GetSeconds();

    for (int i = 0; i < (int)g_ues.size(); i++)
    {
        auto& ue = g_ues[i];
        
        if (ue.context.servingGnb.empty()) {
            std::string bestSat = SelectBestSatellite(i, "");
            if (!bestSat.empty()) {
                ue.context.servingGnb = bestSat;
                ue.measState.servingCellId = bestSat;
                std::cout << "✓ [UE-" << i << "] Initial satellite selected: " << bestSat << std::endl;
            }
        }
        
        std::string activeSat = ue.context.servingGnb;
        
        // 3. Apply physical layer error rate (BER/PER) and delay to this UE's dedicated object
        if (!activeSat.empty() && ue.channelCache.count(activeSat))
        {
            const UeSatMetrics& metrics = ue.channelCache[activeSat].second;
            
            // Calculate PER (Packet Error Rate)
            double snrLinear = std::pow(10.0, metrics.snrDb / 10.0);
            double ber = 0.5 * std::erfc(std::sqrt(snrLinear));
            uint32_t bitsPerPacket = g_packetSize * 8; // Use global configured packet size
            double per = 1.0 - std::pow(1.0 - ber, bitsPerPacket);
            per = std::max(0.0, std::min(1.0, per));
            
            // ★ Handover interruption: If this UE is in handover, force 100% packet loss ★
            if (ue.hoState.interruptionActive) per = 1.0; 
            
            // Apply independent ErrorModel (ensures no interference with other UEs)
            if (ue.ulErrorModel) ue.ulErrorModel->SetAttribute("ErrorRate", DoubleValue(per));
            if (ue.dlErrorModel) ue.dlErrorModel->SetAttribute("ErrorRate", DoubleValue(per));
            
            // Apply independent transmission delay
            double propDelay = metrics.distance / SPEED_OF_LIGHT;
            if (ue.p2pChannel) {
                ue.p2pChannel->SetAttribute("Delay", TimeValue(Seconds(propDelay)));
            }
            
            // ---- Per-UE Channel log ----
            if (g_channelLog.is_open())
            {
                g_channelLog << std::fixed << std::setprecision(3) << simTime
                             << "," << i
                             << "," << activeSat
                             << "," << std::setprecision(3) << (metrics.distance / 1000.0)
                             << "," << std::setprecision(2) << metrics.elevation
                             << "," << std::setprecision(4) << metrics.snrDb
                             << "," << std::setprecision(3) << (metrics.dopplerShift / 1000.0)
                             << std::endl;
            }

            // ---- Per-UE SNR / PER log ----
            if (g_snrLog.is_open())
            {
                g_snrLog << std::fixed << std::setprecision(3) << simTime
                         << "," << i
                         << "," << activeSat
                         << "," << std::setprecision(3) << (metrics.distance / 1000.0)
                         << "," << std::setprecision(2) << metrics.elevation
                         << "," << std::setprecision(4) << metrics.snrDb
                         << "," << std::scientific << std::setprecision(6) << ber
                         << "," << std::fixed << std::setprecision(4) << (per * 100.0)
                         << "," << (ue.hoState.interruptionActive ? 1 : 0)
                         << std::endl;
            }
        }
    }
}

void CheckHandoverConditions()
{
    double simTime = Simulator::Now().GetSeconds();

    // 1. Update channel cache for all UEs and all visible satellites
    for (int i = 0; i < (int)g_ues.size(); i++) {
        for (const auto& pair : g_satellites) {
            UpdateSatelliteState(pair.first, simTime, i);
        }
    }
    
    // 2. Apply latest physical layer attributes (PER, Delay)
    UpdateChannelState();
    
    // 3. Check if each UE needs to trigger measurement report and handover
    for (int i = 0; i < (int)g_ues.size(); i++)
    {
        CheckHandover(simTime, i);
    }
    
    // 4. Schedule next 1-second check
    Simulator::Schedule(Seconds(1.0), &CheckHandoverConditions);
}

int
main(int argc, char* argv[])
{
    // Simulation parameters
    std::string csvFile = "dataset/visible_satellites_hsinchu.csv";
    std::string ueGroupsCsvFile = "dataset/ue/simulation_groups_taiwan3.csv";
    std::vector<std::string> satelliteNames = {"STARLINK-2692", "STARLINK-5801", "STARLINK-1433"};
    double simTime = 300.0;  // 5 minutes to see multiple handovers
    double packetInterval = 0.001; //0.001 too small
    double minElevation = 0.0;  // Minimum elevation to include satellite (0 = use predefined list)
    
    // NTN channel parameters (3GPP TR 38.811 compliant)
    
    g_frequency = 20.0e9;     // 20 GHz Ka-band
    g_txPower = 19.5;        // 19.5 dBm - TX amplifier output power
    g_txAntennaGain = 30.5;   // 30.5 dBi - Satellite antenna power gain
    g_rxGain = 39.7;   // 39.7 dBi - VAST user terminal antenna
    g_noiseFigure = 1.2;         // 1.2 dB - VAST 
    g_bandwidth = 250e6;      // 250 MHz per resource block
    g_fecCodingGain = 1.0;    // 3 dB - realistic FEC gain
    g_packetSize = 1250;

    // Parse command line
    CommandLine cmd;
    cmd.AddValue("csvFile", "Path to SGP4 CSV file", csvFile);
    cmd.AddValue("simTime", "Simulation time (seconds)", simTime);
    cmd.AddValue("txPower", "Transmit power (dBm)", g_txPower);
    cmd.AddValue("fecGain", "FEC coding gain (dB)", g_fecCodingGain);
    cmd.AddValue("minElevation", "Minimum peak elevation to include satellites (0=use predefined list)", minElevation);
    
    // SNR-based handover parameters (PRIMARY)
    cmd.AddValue("reportingOffset", "Event A3 offset for reporting (km)", g_reportingOffset);
    cmd.AddValue("minSNR", "Minimum SNR threshold (dB)", g_minSNR);
    
    // Fallback parameters (elevation, distance)
    cmd.AddValue("maxDistance", "Maximum distance threshold for fallback (km)", g_maxDistance);
    cmd.AddValue("timeToTrigger", "Time-to-trigger duration (seconds)", g_timeToTrigger);
    cmd.AddValue("predictionEvalDelay", "Future evaluation delay for prediction validation (seconds)", g_predictionEvalDelay);

    // CHO two-phase condition parameters
    cmd.AddValue("choExecutionOffset", "CHO Phase-2 execution distance margin (km)", g_choExecutionOffset);
    cmd.AddValue("seed", "Random seed for reproducibility", g_randomSeed);
    
    // UE measurement configuration (3GPP TS 38.331)
    cmd.AddValue("measurementPeriod", "UE measurement period (seconds)", g_measurementPeriod);
    
    cmd.Parse(argc, argv);

    
    // Load UE groups from CSV
    if (!ueGroupsCsvFile.empty())
    {
        if (!LoadUeGroupsFromCsv(ueGroupsCsvFile))
        {
            std::cerr << "Warning: Failed to load UE groups from " << ueGroupsCsvFile << std::endl;
        }
    }

    // Auto-discover satellites if minElevation specified
    if (minElevation > 0.0)
    {
        satelliteNames = DiscoverSatellites(csvFile, minElevation, simTime);
        if (satelliteNames.empty())
        {
            std::cerr << "ERROR: No satellites found with elevation ≥ " << minElevation << "°" << std::endl;
            return 1;
        }
    }
    
     if (minElevation > 0.0)
        std::cout << "Min Elevation Filter: " << minElevation << "°" << std::endl;
    std::cout << "Time-To-Trigger: ";
    if (g_timeToTrigger < 1.0) {
        std::cout << std::fixed << std::setprecision(0) << (g_timeToTrigger * 1000.0) << " ms";
    } else {
        std::cout << std::fixed << std::setprecision(1) << g_timeToTrigger << " s";
    }
    std::cout << std::endl;
    std::cout << "Frequency: " << g_frequency/1e9 << " GHz (Ka-band)" << std::endl;
    std::cout << "TX Power: " << g_txPower << " dBm (" << std::pow(10, (g_txPower-30)/10) << " W)" << std::endl;
    std::cout << "Bandwidth: " << g_bandwidth/1e6 << " MHz" << std::endl;
    std::cout << "Noise Figure: " << g_noiseFigure << " dB" << std::endl;
    std::cout << "FEC Gain: " << g_fecCodingGain << " dB" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    // Convert ground station position to ECEF
    // Hsinchu, Taiwan coordinates (matching SatTrack.py: 24.8°N, 120.97°E, 0m)
    GeographicToECEF(24.8, 120.97, 0.0, g_groundX, g_groundY, g_groundZ);
    
    std::cout << "Ground station (Hsinchu, Taiwan):" << std::endl;
    std::cout << "  Lat/Lon: 24.8°N, 120.97°E, 0m alt" << std::endl;
    std::cout << "  ECEF: (" << g_groundX/1000.0 << ", " 
              << g_groundY/1000.0 << ", " << g_groundZ/1000.0 << ") km\n" << std::endl;
    
    // Load trajectories for all satellites
    for (const auto& satName : satelliteNames)
    {
        if (!LoadTrajectory(csvFile, satName))
        {
            std::cerr << "Failed to load trajectory for " << satName << std::endl;
            return 1;
        }
    }
    
    // std::cout << "\nInitial satellite positions:" << std::endl;
    for (const auto& satName : satelliteNames)
    {
        double sat_x, sat_y, sat_z, _elev_unused, _dist_unused;
        
        // 1. Confirm satellite data exists and trajectory is not empty
        auto it = g_satellites.find(satName);
        if (it == g_satellites.end() || it->second.trajectory.empty()) {
            std::cout << "  [Debug] " << satName << " skipped: Trajectory empty or not found." << std::endl;
            continue;
        }
        
        // // 2. Confirm t=0.0 is within its trajectory time range
        // double startTime = it->second.trajectory.front().time;
        // double endTime = it->second.trajectory.back().time;
        // if (0.0 < startTime || 0.0 > endTime) {
        //     std::cout << "  [Debug] " << satName << " skipped: t=0 not in range [" << startTime << ", " << endTime << "]" << std::endl;
        //     continue;
        // }

        // 2. Get satellite's real ECEF coordinates at t=0.0
        InterpolatePosition(satName, 0.0, sat_x, sat_y, sat_z, _elev_unused, _dist_unused);
        
        // 3. Calculate vector and distance between satellite and ground station (g_groundX, Y, Z)
        double dx = sat_x - g_groundX;
        double dy = sat_y - g_groundY;
        double dz = sat_z - g_groundZ;
        double real_distance = std::sqrt(dx*dx + dy*dy + dz*dz);
        
        if (real_distance < 1.0) continue;

        // 4. Calculate elevation angle (project to ground station's local vertical vector)
        double ground_r = std::sqrt(g_groundX*g_groundX + g_groundY*g_groundY + g_groundZ*g_groundZ);
        double dot_rn = dx*(g_groundX/ground_r) + dy*(g_groundY/ground_r) + dz*(g_groundZ/ground_r);
        double real_elevation = std::asin(std::max(-1.0, std::min(1.0, dot_rn / real_distance))) * 180.0 / M_PI;

        // 5. Print results (only print visible satellites with elevation >= 0)
    //         std::cout << "  " << satName << ":" << std::endl;
    //         std::cout << "    Distance: " << real_distance / 1000.0 << " km, "
    //                   << "Elevation: " << real_elevation << "°" << std::endl;
    //     } else {
    //         std::cout << "  [Debug] " << satName << " skipped: Elevation < 0 (" << real_elevation << "°)" << std::endl;
    //     }
        std::cout << "  " << satName << ":" << std::endl;
        std::cout << "    Distance: " << real_distance / 1000.0 << " km, "
                    << "Elevation: " << real_elevation << "°" << std::endl;
    }
    std::cout << std::endl;
    
    // Create nodes
    NodeContainer groundNodes, satNodes;
    groundNodes.Create(g_ues.size());
    satNodes.Create(1);
    
    InternetStackHelper internet;
    internet.Install(groundNodes);
    internet.Install(satNodes);

    uint16_t port = 9;
    UdpEchoServerHelper echoServer(port);
    ApplicationContainer serverApps = echoServer.Install(satNodes.Get(0));
    serverApps.Start(Seconds(0.0));
    serverApps.Stop(Seconds(simTime));

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    
    Ipv4AddressHelper ipv4;

    for (uint32_t i = 0; i < g_ues.size(); i++)
    {
        // 1. Connection: groundNodes.Get(i) <--> satNodes.Get(0)
        NetDeviceContainer devices = p2p.Install(groundNodes.Get(i), satNodes.Get(0));

        // 2. Assign independent IP subnet (e.g., 10.1.1.0, 10.1.2.0, ...)
        // Each P2P link needs an independent subnet to avoid routing conflicts
        std::ostringstream subnet;
        subnet << "10.1." << (i + 1) << ".0";
        ipv4.SetBase(subnet.str().c_str(), "255.255.255.0");
        Ipv4InterfaceContainer interfaces = ipv4.Assign(devices);

        // 3. Extract dedicated Channel and store in UE Info, allowing UpdateChannelState to update its delay
        Ptr<PointToPointNetDevice> p2pDevice = DynamicCast<PointToPointNetDevice>(devices.Get(0));
        g_ues[i].p2pChannel = DynamicCast<PointToPointChannel>(p2pDevice->GetChannel());

        // 4. Create dedicated Error Models for this UE
        g_ues[i].dlErrorModel = CreateObject<RateErrorModel>();
        g_ues[i].dlErrorModel->SetAttribute("ErrorUnit", EnumValue(RateErrorModel::ERROR_UNIT_PACKET));

        g_ues[i].ulErrorModel = CreateObject<RateErrorModel>();
        g_ues[i].ulErrorModel->SetAttribute("ErrorUnit", EnumValue(RateErrorModel::ERROR_UNIT_PACKET));

        // Device index 0 = UE (receives downlink), index 1 = satellite (receives uplink)
        devices.Get(0)->SetAttribute("ReceiveErrorModel", PointerValue(g_ues[i].dlErrorModel));
        devices.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(g_ues[i].ulErrorModel));

        // 5. Install UDP Client on UE
        UdpEchoClientHelper echoClient(interfaces.GetAddress(1), port);
        echoClient.SetAttribute("MaxPackets", UintegerValue(uint32_t(simTime / packetInterval) + 10));
        echoClient.SetAttribute("Interval", TimeValue(Seconds(packetInterval)));
        echoClient.SetAttribute("PacketSize", UintegerValue(g_packetSize));

        ApplicationContainer clientApps = echoClient.Install(groundNodes.Get(i));
        clientApps.Start(Seconds(1.0 + i * 0.05)); 
        clientApps.Stop(Seconds(simTime));
    }
    
    // Set up flow monitor (must be in main() scope so flowmon.GetClassifier() works post-run)
    FlowMonitorHelper flowmon;
    g_flowMonitor = flowmon.InstallAll();
    
    
    // Keep this algorithm's generated CSV files separate from other strategies.
    mkdir("result", 0755);
    std::string outputDir = "result/distance-cho";
    mkdir(outputDir.c_str(), 0755);
    
    // Open log files in the output directory
    g_channelLog.open(outputDir + "/sgp4-multi-channel-log.csv");
    g_channelLog << "time_s,ue_id,active_satellite,distance_km,elevation_deg,snr_dB,doppler_kHz" << std::endl;
    
    g_snrLog.open(outputDir + "/sgp4-multi-snr-log.csv");
    g_snrLog << "time_s,ue_id,active_satellite,distance_km,elevation_deg,snr_dB,ber,per_percent,ho_interrupting" << std::endl;
    
    g_handoverLog.open(outputDir + "/sgp4-handover-log.csv");
    g_handoverLog << "time_s,ue_id,old_satellite,new_satellite,old_snr_dB,new_snr_dB,doppler_jump_kHz,old_dist_km,new_dist_km" << std::endl;
    
    // New: 5G Xn Interface signaling log
    g_xnSignalingLog.open(outputDir + "/sgp4-xn-signaling-log.csv");
    g_xnSignalingLog << "time_s,ue_id,message_type,source,destination,interface,description" << std::endl;
    
    // CHO detailed event log
    g_choLog.open(outputDir + "/cho-detailed-log.csv");
    g_choLog << "time_s,ue_id,event,source_cell,target_cell,serving_sinr_dB,target_sinr_dB,delay_ms,value,notes" << std::endl;
    
    g_allSatellitesLog.open(outputDir + "/sgp4-all-satellites-snr.csv");
    g_allSatellitesLog << "time_s";
    for (const auto& satName : satelliteNames) {
        g_allSatellitesLog << "," << satName << "_snr_dB";
    }
    g_allSatellitesLog << std::endl;
    
    // Schedule periodic tasks
    // - Channel updates: high frequency (1s) for accurate tracking
    // - Handover checks: separate scheduling to avoid conflicts
    Simulator::Schedule(Seconds(0.0), &CheckHandoverConditions);
    
    // Run simulation
    std::cout << "▶️  Starting simulation for " << simTime << " seconds...\n" << std::endl;
    
    // Print initial link quality
    Simulator::Schedule(Seconds(2.0), &PrintLinkQuality, "Initial");
    
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    
    // Print statistics
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "SIMULATION COMPLETE" << std::endl;
    std::cout << std::string(70, '=') << std::endl;
    
    g_flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = g_flowMonitor->GetFlowStats();

    // ---- Aggregate per-UE flow statistics ----
    // Each UE i uses subnet 10.1.(i+1).0/24.
    // FlowMonitor captures both uplink (UE→sat) and downlink (sat→UE) flows;
    // we sum them together for a unified Tx/Rx count per UE.
    struct UeFlowStats {
        uint32_t txPackets   = 0;
        uint32_t rxPackets   = 0;
        uint32_t lostPackets = 0;
        uint64_t rxBytes     = 0;   // received bytes (for throughput)
        double   delayMsTotal = 0.0; // sum of per-packet delays in ms (for true avg latency)
        double   firstTxTime = 1e18;
        double   lastRxTime  = 0.0;
    };
    std::vector<UeFlowStats> ueFlowStats(g_ues.size());

    for (auto& kv : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(kv.first);
        const FlowMonitor::FlowStats& fs = kv.second;

        for (int i = 0; i < (int)g_ues.size(); i++)
        {
            // Subnet base: 10.1.(i+1).0  mask: 255.255.255.0
            uint32_t base = (10u << 24) | (1u << 16) | ((uint32_t)(i + 1) << 8);
            uint32_t mask = 0xFFFFFF00u;
            if ((t.sourceAddress.Get() & mask) == base ||
                (t.destinationAddress.Get() & mask) == base)
            {
                ueFlowStats[i].txPackets    += fs.txPackets;
                ueFlowStats[i].rxPackets    += fs.rxPackets;
                ueFlowStats[i].lostPackets  += fs.lostPackets;
                ueFlowStats[i].rxBytes      += fs.rxBytes;
                ueFlowStats[i].delayMsTotal += fs.delaySum.GetMilliSeconds();
                if (fs.timeFirstTxPacket.GetSeconds() < ueFlowStats[i].firstTxTime)
                    ueFlowStats[i].firstTxTime = fs.timeFirstTxPacket.GetSeconds();
                if (fs.timeLastRxPacket.GetSeconds() > ueFlowStats[i].lastRxTime)
                    ueFlowStats[i].lastRxTime = fs.timeLastRxPacket.GetSeconds();
                break;
            }
        }
    }

    // Global totals
    uint32_t totalTx = 0, totalRx = 0;
    for (auto& ufs : ueFlowStats) { totalTx += ufs.txPackets; totalRx += ufs.rxPackets; }
    double pdr = totalTx > 0 ? (double)totalRx / totalTx * 100.0 : 0.0;
    NS_LOG_UNCOND("Overall PDR: " << pdr << "%");  // suppress unused-variable warning

    // Per-UE breakdown table
    {
        std::cout << "\n📋 PER-UE HANDOVER & PDR BREAKDOWN:" << std::endl;
        const int W = 132;
        std::cout << "  " << std::string(W, '-') << std::endl;
        std::cout << "  " << std::left
                  << std::setw(6)  << "UE"
                  << std::setw(14) << "Group"
                  << std::setw(12) << "Scenario"
                  << std::setw(8)  << "Users"
                  << std::setw(8)  << "Lat"
                  << std::setw(8)  << "Lon"
                  << std::setw(8)  << "HO-OK"
                  << std::setw(10) << "HO-Fail"
                  << std::setw(10) << "MR-Report"
                  << std::setw(18) << "Successful-Rate%"
                //   << std::setw(8)  << "Tx"
                //   << std::setw(8)  << "Rx"
                  << std::setw(8)  << "PDR%"
                  << std::setw(12) << "Tput(Kbps)"
                  << std::setw(12) << "Latency(ms)" << std::endl;
        std::cout << "  " << std::string(W, '-') << std::endl;

        int totalUeHoOk = 0, totalUeHoFail = 0;
        uint32_t totalUeTx = 0, totalUeRx = 0;
        double totalTputKbps = 0.0;

        for (int i = 0; i < (int)g_ues.size(); i++)
        {
            const auto& ue  = g_ues[i];
            const auto& ufs = ueFlowStats[i];
            totalUeHoOk   += ue.hoCount;
            totalUeHoFail += ue.hoFail;
            totalUeTx     += ufs.txPackets;
            totalUeRx     += ufs.rxPackets;

            double uePdr     = ufs.txPackets > 0
                               ? (double)ufs.rxPackets / ufs.txPackets * 100.0 : 0.0;
            // Average end-to-end latency per received packet
            double ueLatency = ufs.rxPackets > 0
                               ? ufs.delayMsTotal / ufs.rxPackets : 0.0;
            // Goodput: received bytes over the active window of this UE's flows
            double duration  = (ufs.lastRxTime > ufs.firstTxTime && ufs.firstTxTime < 1e17)
                               ? (ufs.lastRxTime - ufs.firstTxTime) : simTime;
            double tputKbps  = duration > 0
                               ? (ufs.rxBytes * 8.0) / duration / 1000.0 : 0.0;
            totalTputKbps   += tputKbps;
            int totalAttempts = ue.hoCount + ue.hoFail;
            double successRate = totalAttempts > 0
                                     ? (100.0 * ue.hoCount / totalAttempts)
                                     : 0.0;

            std::cout << "  " << std::left
                      << std::setw(6)  << ("UE-" + std::to_string(i))
                      << std::setw(14) << ue.groupName.substr(0, 13)
                      << std::setw(12) << ue.scenario.substr(0, 11)
                      << std::setw(8)  << ue.numUsers
                      << std::setw(8)  << std::fixed << std::setprecision(2) << ue.lat
                      << std::setw(8)  << std::fixed << std::setprecision(2) << ue.lon
                      << std::setw(8)  << ue.hoCount
                      << std::setw(10) << ue.hoFail
                      << std::setw(10) << totalAttempts
                      << std::setw(18) << std::fixed << std::setprecision(2) << successRate
                    //   << std::setw(8)  << ufs.txPackets
                    //   << std::setw(8)  << ufs.rxPackets
                      << std::setw(8)  << std::fixed << std::setprecision(2) << uePdr
                      << std::setw(12) << std::fixed << std::setprecision(2) << tputKbps
                      << std::setw(12) << std::fixed << std::setprecision(2) << ueLatency
                      << std::endl;
        }

        double totalPdr = totalUeTx > 0 ? (double)totalUeRx / totalUeTx * 100.0 : 0.0;
        std::cout << "  " << std::string(W, '-') << std::endl;
        std::cout << "  " << std::left
                  << std::setw(6)  << "TOTAL"
                  << std::setw(14) << ""
                  << std::setw(12) << ""
                  << std::setw(8)  << ""
                  << std::setw(8)  << ""
                  << std::setw(8)  << ""
                  << std::setw(8)  << totalUeHoOk
                  << std::setw(10) << totalUeHoFail
                  << std::setw(10) << ""
                  << std::setw(18) << ""
                  //   << std::setw(8)  << totalUeTx
                  //   << std::setw(8)  << totalUeRx
                  << std::setw(8)  << std::fixed << std::setprecision(2) << totalPdr
                  << std::setw(12) << std::fixed << std::setprecision(2) << totalTputKbps
                  << std::setw(12) << ""
                  << std::endl;
        std::cout << "  " << std::string(W, '-') << std::endl;
    }

    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "✅ Simulation completed successfully!" << std::endl;
    std::cout << std::string(70, '=') << std::endl << std::endl;
    
    g_channelLog.close();
    g_snrLog.close();
    g_handoverLog.close();
    g_xnSignalingLog.close();
    
    Simulator::Destroy();
    
    return 0;
}
