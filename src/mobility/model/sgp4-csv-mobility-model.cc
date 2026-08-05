/*
 * Copyright (c) 2025
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 */

#include "sgp4-csv-mobility-model.h"

#include "ns3/boolean.h"
#include "ns3/log.h"
#include "ns3/simulator.h"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("Sgp4CsvMobilityModel");
NS_OBJECT_ENSURE_REGISTERED(Sgp4CsvMobilityModel);

TypeId
Sgp4CsvMobilityModel::GetTypeId(void)
{
    static TypeId tid =
        TypeId("ns3::Sgp4CsvMobilityModel")
            .SetParent<GeocentricConstantPositionMobilityModel>()
            .SetGroupName("Mobility")
            .AddConstructor<Sgp4CsvMobilityModel>()
            .AddAttribute("UseEcefCoordinates",
                          "Use ECEF coordinates from CSV instead of geographic coordinates",
                          BooleanValue(true),
                          MakeBooleanAccessor(&Sgp4CsvMobilityModel::m_useEcef),
                          MakeBooleanChecker());
    return tid;
}

Sgp4CsvMobilityModel::Sgp4CsvMobilityModel()
    : m_useEcef(true),
      m_startTime(0.0),
      m_trajectoryLoaded(false)
{
    NS_LOG_FUNCTION(this);
}

Sgp4CsvMobilityModel::~Sgp4CsvMobilityModel()
{
    NS_LOG_FUNCTION(this);
}

void
Sgp4CsvMobilityModel::SetUseEcefCoordinates(bool useEcef)
{
    m_useEcef = useEcef;
}

double
Sgp4CsvMobilityModel::ParseTimestamp(const std::string& timestamp) const
{
    // Parse timestamp format: "2025-11-26 14:16:50"
    std::tm tm = {};
    std::istringstream ss(timestamp);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    
    if (ss.fail())
    {
        NS_LOG_ERROR("Failed to parse timestamp: " << timestamp);
        return 0.0;
    }
    
    std::time_t time = std::mktime(&tm);
    return static_cast<double>(time);
}

bool
Sgp4CsvMobilityModel::LoadTrajectory(const std::string& filename, const std::string& satelliteName)
{
    NS_LOG_FUNCTION(this << filename << satelliteName);
    
    m_satelliteName = satelliteName;
    m_trajectory.clear();
    
    std::ifstream file(filename);
    if (!file.is_open())
    {
        NS_LOG_ERROR("Cannot open trajectory file: " << filename);
        return false;
    }
    
    std::string line;
    // Skip header line
    std::getline(file, line);
    
    double firstTime = -1.0;
    int lineCount = 0;
    
    while (std::getline(file, line))
    {
        lineCount++;
        std::istringstream ss(line);
        std::string field;
        std::vector<std::string> fields;
        
        // Parse CSV line
        while (std::getline(ss, field, ','))
        {
            fields.push_back(field);
        }
        
        if (fields.size() < 11)
        {
            NS_LOG_WARN("Skipping malformed line " << lineCount);
            continue;
        }
        
        // Check if this line is for our satellite
        std::string satName = fields[1];
        if (satName != satelliteName)
        {
            continue;
        }
        
        PositionData data;
        
        try
        {
            // Parse timestamp
            double timestamp = ParseTimestamp(fields[0]);
            if (firstTime < 0)
            {
                firstTime = timestamp;
                m_startTime = firstTime;
            }
            data.time = timestamp - firstTime; // Relative time in seconds
            
            // Parse other fields
            data.elevation = std::stod(fields[2]);
            data.azimuth = std::stod(fields[3]);
            data.distance = std::stod(fields[4]) * 1000.0; // Convert km to meters
            data.lat = std::stod(fields[5]);
            data.lon = std::stod(fields[6]);
            data.alt = std::stod(fields[7]) * 1000.0; // Convert km to meters
            data.x_ecef = std::stod(fields[8]) * 1000.0; // Convert km to meters
            data.y_ecef = std::stod(fields[9]) * 1000.0;
            data.z_ecef = std::stod(fields[10]) * 1000.0;
            
            m_trajectory.push_back(data);
        }
        catch (const std::exception& e)
        {
            NS_LOG_WARN("Error parsing line " << lineCount << ": " << e.what());
            continue;
        }
    }
    
    file.close();
    
    if (m_trajectory.empty())
    {
        NS_LOG_ERROR("No data found for satellite: " << satelliteName);
        return false;
    }
    
    // Sort by time (should already be sorted, but just in case)
    std::sort(m_trajectory.begin(), m_trajectory.end(),
              [](const PositionData& a, const PositionData& b) { return a.time < b.time; });
    
    m_trajectoryLoaded = true;
    
    NS_LOG_INFO("Loaded " << m_trajectory.size() << " trajectory points for " << satelliteName);
    NS_LOG_INFO("Time span: " << m_trajectory.front().time << " to " << m_trajectory.back().time << " seconds");
    
    // Set initial position
    if (m_useEcef)
    {
        Vector ecefPos(m_trajectory[0].x_ecef, m_trajectory[0].y_ecef, m_trajectory[0].z_ecef);
        SetGeocentricPosition(ecefPos);
    }
    else
    {
        Vector geoPos(m_trajectory[0].lat, m_trajectory[0].lon, m_trajectory[0].alt);
        SetGeographicPosition(geoPos);
    }
    
    // Schedule periodic position updates
    Simulator::Schedule(Seconds(1.0), &Sgp4CsvMobilityModel::UpdatePosition, this);
    
    return true;
}

Vector
Sgp4CsvMobilityModel::InterpolatePosition(double t) const
{
    if (!m_trajectoryLoaded || m_trajectory.empty())
    {
        return Vector(0, 0, 0);
    }
    
    // Handle edge cases
    if (t <= m_trajectory.front().time)
    {
        if (m_useEcef)
        {
            return Vector(m_trajectory.front().x_ecef, 
                         m_trajectory.front().y_ecef, 
                         m_trajectory.front().z_ecef);
        }
        else
        {
            return Vector(m_trajectory.front().lat, 
                         m_trajectory.front().lon, 
                         m_trajectory.front().alt);
        }
    }
    
    if (t >= m_trajectory.back().time)
    {
        if (m_useEcef)
        {
            return Vector(m_trajectory.back().x_ecef, 
                         m_trajectory.back().y_ecef, 
                         m_trajectory.back().z_ecef);
        }
        else
        {
            return Vector(m_trajectory.back().lat, 
                         m_trajectory.back().lon, 
                         m_trajectory.back().alt);
        }
    }
    
    // Find the two points to interpolate between
    auto it = std::lower_bound(m_trajectory.begin(), m_trajectory.end(), t,
                               [](const PositionData& data, double time) {
                                   return data.time < time;
                               });
    
    if (it == m_trajectory.begin())
    {
        it++;
    }
    
    const PositionData& p1 = *(it - 1);
    const PositionData& p2 = *it;
    
    // Linear interpolation
    double alpha = (t - p1.time) / (p2.time - p1.time);
    
    if (m_useEcef)
    {
        double x = p1.x_ecef + alpha * (p2.x_ecef - p1.x_ecef);
        double y = p1.y_ecef + alpha * (p2.y_ecef - p1.y_ecef);
        double z = p1.z_ecef + alpha * (p2.z_ecef - p1.z_ecef);
        return Vector(x, y, z);
    }
    else
    {
        double lat = p1.lat + alpha * (p2.lat - p1.lat);
        double lon = p1.lon + alpha * (p2.lon - p1.lon);
        double alt = p1.alt + alpha * (p2.alt - p1.alt);
        return Vector(lat, lon, alt);
    }
}

Vector
Sgp4CsvMobilityModel::CalculateVelocity(double t) const
{
    if (!m_trajectoryLoaded || m_trajectory.size() < 2)
    {
        return Vector(0, 0, 0);
    }
    
    // Use finite difference with small time step
    double dt = 1.0; // 1 second
    Vector p1 = InterpolatePosition(t);
    Vector p2 = InterpolatePosition(t + dt);
    
    return Vector((p2.x - p1.x) / dt, (p2.y - p1.y) / dt, (p2.z - p1.z) / dt);
}

void
Sgp4CsvMobilityModel::UpdatePosition()
{
    if (!m_trajectoryLoaded)
    {
        return;
    }
    
    double simTime = Simulator::Now().GetSeconds();
    
    // Check if simulation is still within trajectory bounds
    if (simTime > m_trajectory.back().time)
    {
        NS_LOG_INFO("Simulation time beyond trajectory data");
        return;
    }
    
    Vector pos = InterpolatePosition(simTime);
    
    if (m_useEcef)
    {
        SetGeocentricPosition(pos);
    }
    else
    {
        SetGeographicPosition(pos);
    }
    
    // Schedule next update
    Simulator::Schedule(Seconds(1.0), &Sgp4CsvMobilityModel::UpdatePosition, this);
}

Vector
Sgp4CsvMobilityModel::DoGetVelocity(void) const
{
    if (!m_trajectoryLoaded)
    {
        return Vector(0, 0, 0);
    }
    
    double simTime = Simulator::Now().GetSeconds();
    return CalculateVelocity(simTime);
}

} // namespace ns3
