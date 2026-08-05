/*
 * Copyright (c) 2023 SIGNET Lab, Department of Information Engineering,
 * University of Padova
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ns3/abort.h"
#include "ns3/boolean.h"
#include "ns3/channel-condition-model.h"
#include "ns3/config.h"
#include "ns3/constant-position-mobility-model.h"
#include "ns3/constant-velocity-mobility-model.h"
#include "ns3/double.h"
#include "ns3/geocentric-constant-position-mobility-model.h"
#include "ns3/log.h"
#include "ns3/mobility-helper.h"
#include "ns3/simulator.h"
#include "ns3/test.h"
#include "ns3/three-gpp-propagation-loss-model.h"
#include "ns3/three-gpp-v2v-propagation-loss-model.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("ThreeGppNTNPropagationLossModelsTest");

/**
 * \ingroup propagation-tests
 *
 * Test case for the class ThreeGppNTNDenseUrbanPropagationLossModel.
 * It computes the pathloss between two nodes and compares it with the value
 * obtained using the results provided in 3GPP TR 38.821.
 */
class ThreeGppNTNDenseUrbanPropagationLossModelTestCase : public TestCase
{
  public:
    /**
     * Constructor
     */
    ThreeGppNTNDenseUrbanPropagationLossModelTestCase();

    /**
     * Destructor
     */
    ~ThreeGppNTNDenseUrbanPropagationLossModelTestCase() override;

  private:
    /**
     * Build the simulation scenario and run the tests
     */
    void DoRun() override;

    /**
     * Struct containing the parameters for each test
     */
    struct TestVector
    {
        double m_distance;  //!< 2D distance between UT and BS in meters
        bool m_isLos;       //!< if true LOS, if false NLOS
        double m_frequency; //!< carrier frequency in Hz
        double m_pt;        //!< transmitted power in dBm
        double m_pr;        //!< received power in dBm
    };

    TestVectors<TestVector> m_testVectors; //!< array containing all the test vectors
    double m_tolerance;                    //!< tolerance
};

ThreeGppNTNDenseUrbanPropagationLossModelTestCase::
    ThreeGppNTNDenseUrbanPropagationLossModelTestCase()
    : TestCase("Test for the ThreeNTNDenseUrbanPropagationLossModel class"),
      m_testVectors(),
      m_tolerance(5e-2)
{
}

ThreeGppNTNDenseUrbanPropagationLossModelTestCase::
    ~ThreeGppNTNDenseUrbanPropagationLossModelTestCase()
{
}

void
ThreeGppNTNDenseUrbanPropagationLossModelTestCase::DoRun()
{
    TestVector testVector;

    testVector.m_distance = 35786000;
    testVector.m_isLos = true;
    testVector.m_frequency = 20.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -209.915;
    m_testVectors.Add(testVector);

    testVector.m_distance = 35786000;
    testVector.m_isLos = true;
    testVector.m_frequency = 30.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -213.437;
    m_testVectors.Add(testVector);

    testVector.m_distance = 35786000;
    testVector.m_isLos = true;
    testVector.m_frequency = 2.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -191.744;
    m_testVectors.Add(testVector);

    testVector.m_distance = 600000;
    testVector.m_isLos = true;
    testVector.m_frequency = 20.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -174.404;
    m_testVectors.Add(testVector);

    testVector.m_distance = 600000;
    testVector.m_isLos = true;
    testVector.m_frequency = 30.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -177.925;
    m_testVectors.Add(testVector);

    testVector.m_distance = 600000;
    testVector.m_isLos = true;
    testVector.m_frequency = 2.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -156.233;
    m_testVectors.Add(testVector);

    testVector.m_distance = 1200000;
    testVector.m_isLos = true;
    testVector.m_frequency = 20.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -180.424;
    m_testVectors.Add(testVector);

    testVector.m_distance = 1200000;
    testVector.m_isLos = true;
    testVector.m_frequency = 30.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -183.946;
    m_testVectors.Add(testVector);

    testVector.m_distance = 1200000;
    testVector.m_isLos = true;
    testVector.m_frequency = 2.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -162.253;
    m_testVectors.Add(testVector);

    // Create the nodes for BS and UT
    NodeContainer nodes;
    nodes.Create(2);

    // Create the mobility models
    Ptr<MobilityModel> a = CreateObject<GeocentricConstantPositionMobilityModel>();
    nodes.Get(0)->AggregateObject(a);
    Ptr<MobilityModel> b = CreateObject<GeocentricConstantPositionMobilityModel>();
    nodes.Get(1)->AggregateObject(b);

    // Use a deterministic channel condition model
    Ptr<ChannelConditionModel> losCondModel = CreateObject<AlwaysLosChannelConditionModel>();
    Ptr<ChannelConditionModel> nlosCondModel = CreateObject<NeverLosChannelConditionModel>();

    // Create the propagation loss model
    Ptr<ThreeGppNTNDenseUrbanPropagationLossModel> lossModel =
        CreateObject<ThreeGppNTNDenseUrbanPropagationLossModel>();
    lossModel->SetAttribute("ShadowingEnabled", BooleanValue(false)); // disable the shadow fading

    for (std::size_t i = 0; i < m_testVectors.GetN(); i++)
    {
        TestVector testVector = m_testVectors.Get(i);

        Vector posA = Vector(0.0, 0.0, 0.0);
        Vector posB = Vector(0.0, 0.0, testVector.m_distance);

        // set the LOS or NLOS condition
        if (testVector.m_isLos)
        {
            lossModel->SetChannelConditionModel(losCondModel);
        }
        else
        {
            lossModel->SetChannelConditionModel(nlosCondModel);
        }

        a->SetPosition(posA);
        b->SetPosition(posB);

        lossModel->SetAttribute("Frequency", DoubleValue(testVector.m_frequency));
        NS_TEST_EXPECT_MSG_EQ_TOL(lossModel->CalcRxPower(testVector.m_pt, a, b),
                                  testVector.m_pr,
                                  m_tolerance,
                                  "Got unexpected rcv power");
    }

    Simulator::Destroy();
}

/**
 * \ingroup propagation-tests
 *
 * Test case for the class ThreeGppNTNUrbanPropagationLossModel.
 * It computes the pathloss between two nodes and compares it with the value
 * obtained using the results provided in 3GPP TR 38.821.
 */
class ThreeGppNTNUrbanPropagationLossModelTestCase : public TestCase
{
  public:
    /**
     * Constructor
     */
    ThreeGppNTNUrbanPropagationLossModelTestCase();

    /**
     * Destructor
     */
    ~ThreeGppNTNUrbanPropagationLossModelTestCase() override;

  private:
    /**
     * Build the simulation scenario and run the tests
     */
    void DoRun() override;

    /**
     * Struct containing the parameters for each test
     */
    struct TestVector
    {
        double m_distance;  //!< 2D distance between UT and BS in meters
        bool m_isLos;       //!< if true LOS, if false NLOS
        double m_frequency; //!< carrier frequency in Hz
        double m_pt;        //!< transmitted power in dBm
        double m_pr;        //!< received power in dBm
    };

    TestVectors<TestVector> m_testVectors; //!< array containing all the test vectors
    double m_tolerance;                    //!< tolerance
};

ThreeGppNTNUrbanPropagationLossModelTestCase::ThreeGppNTNUrbanPropagationLossModelTestCase()
    : TestCase("Test for the ThreeNTNDenseUrbanPropagationLossModel class"),
      m_testVectors(),
      m_tolerance(5e-2)
{
}

ThreeGppNTNUrbanPropagationLossModelTestCase::~ThreeGppNTNUrbanPropagationLossModelTestCase()
{
}

void
ThreeGppNTNUrbanPropagationLossModelTestCase::DoRun()
{
    TestVector testVector;

    testVector.m_distance = 35786000;
    testVector.m_isLos = true;
    testVector.m_frequency = 20.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -209.915;
    m_testVectors.Add(testVector);

    testVector.m_distance = 35786000;
    testVector.m_isLos = true;
    testVector.m_frequency = 30.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -213.437;
    m_testVectors.Add(testVector);

    testVector.m_distance = 35786000;
    testVector.m_isLos = true;
    testVector.m_frequency = 2.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -191.744;
    m_testVectors.Add(testVector);

    testVector.m_distance = 600000;
    testVector.m_isLos = true;
    testVector.m_frequency = 20.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -174.404;
    m_testVectors.Add(testVector);

    testVector.m_distance = 600000;
    testVector.m_isLos = true;
    testVector.m_frequency = 30.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -177.925;
    m_testVectors.Add(testVector);

    testVector.m_distance = 600000;
    testVector.m_isLos = true;
    testVector.m_frequency = 2.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -156.233;
    m_testVectors.Add(testVector);

    testVector.m_distance = 1200000;
    testVector.m_isLos = true;
    testVector.m_frequency = 20.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -180.424;
    m_testVectors.Add(testVector);

    testVector.m_distance = 1200000;
    testVector.m_isLos = true;
    testVector.m_frequency = 30.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -183.946;
    m_testVectors.Add(testVector);

    testVector.m_distance = 1200000;
    testVector.m_isLos = true;
    testVector.m_frequency = 2.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -162.253;
    m_testVectors.Add(testVector);

    // Create the nodes for BS and UT
    NodeContainer nodes;
    nodes.Create(2);

    // Create the mobility models
    Ptr<MobilityModel> a = CreateObject<GeocentricConstantPositionMobilityModel>();
    nodes.Get(0)->AggregateObject(a);
    Ptr<MobilityModel> b = CreateObject<GeocentricConstantPositionMobilityModel>();
    nodes.Get(1)->AggregateObject(b);

    // Use a deterministic channel condition model
    Ptr<ChannelConditionModel> losCondModel = CreateObject<AlwaysLosChannelConditionModel>();
    Ptr<ChannelConditionModel> nlosCondModel = CreateObject<NeverLosChannelConditionModel>();

    // Create the propagation loss model
    Ptr<ThreeGppNTNUrbanPropagationLossModel> lossModel =
        CreateObject<ThreeGppNTNUrbanPropagationLossModel>();
    lossModel->SetAttribute("ShadowingEnabled", BooleanValue(false)); // disable the shadow fading

    for (std::size_t i = 0; i < m_testVectors.GetN(); i++)
    {
        TestVector testVector = m_testVectors.Get(i);

        Vector posA = Vector(0.0, 0.0, 0.0);
        Vector posB = Vector(0.0, 0.0, testVector.m_distance);

        // set the LOS or NLOS condition
        if (testVector.m_isLos)
        {
            lossModel->SetChannelConditionModel(losCondModel);
        }
        else
        {
            lossModel->SetChannelConditionModel(nlosCondModel);
        }

        a->SetPosition(posA);
        b->SetPosition(posB);

        lossModel->SetAttribute("Frequency", DoubleValue(testVector.m_frequency));
        NS_TEST_EXPECT_MSG_EQ_TOL(lossModel->CalcRxPower(testVector.m_pt, a, b),
                                  testVector.m_pr,
                                  m_tolerance,
                                  "Got unexpected rcv power");
    }

    Simulator::Destroy();
}

/**
 * \ingroup propagation-tests
 *
 * Test case for the class ThreeGppNTNSuburbanPropagationLossModel.
 * It computes the pathloss between two nodes and compares it with the value
 * obtained using the results provided in 3GPP TR 38.821.
 */
class ThreeGppNTNSuburbanPropagationLossModelTestCase : public TestCase
{
  public:
    /**
     * Constructor
     */
    ThreeGppNTNSuburbanPropagationLossModelTestCase();

    /**
     * Destructor
     */
    ~ThreeGppNTNSuburbanPropagationLossModelTestCase() override;

  private:
    /**
     * Build the simulation scenario and run the tests
     */
    void DoRun() override;

    /**
     * Struct containing the parameters for each test
     */
    struct TestVector
    {
        double m_distance;  //!< 2D distance between UT and BS in meters
        bool m_isLos;       //!< if true LOS, if false NLOS
        double m_frequency; //!< carrier frequency in Hz
        double m_pt;        //!< transmitted power in dBm
        double m_pr;        //!< received power in dBm
    };

    TestVectors<TestVector> m_testVectors; //!< array containing all the test vectors
    double m_tolerance;                    //!< tolerance
};

ThreeGppNTNSuburbanPropagationLossModelTestCase::ThreeGppNTNSuburbanPropagationLossModelTestCase()
    : TestCase("Test for the ThreeNTNDenseUrbanPropagationLossModel class"),
      m_testVectors(),
      m_tolerance(5e-2)
{
}

ThreeGppNTNSuburbanPropagationLossModelTestCase::~ThreeGppNTNSuburbanPropagationLossModelTestCase()
{
}

void
ThreeGppNTNSuburbanPropagationLossModelTestCase::DoRun()
{
    TestVector testVector;

    testVector.m_distance = 35786000;
    testVector.m_isLos = true;
    testVector.m_frequency = 20.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -209.915;
    m_testVectors.Add(testVector);

    testVector.m_distance = 35786000;
    testVector.m_isLos = true;
    testVector.m_frequency = 30.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -213.437;
    m_testVectors.Add(testVector);

    testVector.m_distance = 35786000;
    testVector.m_isLos = true;
    testVector.m_frequency = 2.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -191.744;
    m_testVectors.Add(testVector);

    testVector.m_distance = 600000;
    testVector.m_isLos = true;
    testVector.m_frequency = 20.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -174.404;
    m_testVectors.Add(testVector);

    testVector.m_distance = 600000;
    testVector.m_isLos = true;
    testVector.m_frequency = 30.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -177.925;
    m_testVectors.Add(testVector);

    testVector.m_distance = 600000;
    testVector.m_isLos = true;
    testVector.m_frequency = 2.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -156.233;
    m_testVectors.Add(testVector);

    testVector.m_distance = 1200000;
    testVector.m_isLos = true;
    testVector.m_frequency = 20.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -180.424;
    m_testVectors.Add(testVector);

    testVector.m_distance = 1200000;
    testVector.m_isLos = true;
    testVector.m_frequency = 30.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -183.946;
    m_testVectors.Add(testVector);

    testVector.m_distance = 1200000;
    testVector.m_isLos = true;
    testVector.m_frequency = 2.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -162.253;
    m_testVectors.Add(testVector);

    // Create the nodes for BS and UT
    NodeContainer nodes;
    nodes.Create(2);

    // Create the mobility models
    Ptr<MobilityModel> a = CreateObject<GeocentricConstantPositionMobilityModel>();
    nodes.Get(0)->AggregateObject(a);
    Ptr<MobilityModel> b = CreateObject<GeocentricConstantPositionMobilityModel>();
    nodes.Get(1)->AggregateObject(b);

    // Use a deterministic channel condition model
    Ptr<ChannelConditionModel> losCondModel = CreateObject<AlwaysLosChannelConditionModel>();
    Ptr<ChannelConditionModel> nlosCondModel = CreateObject<NeverLosChannelConditionModel>();

    // Create the propagation loss model
    Ptr<ThreeGppNTNSuburbanPropagationLossModel> lossModel =
        CreateObject<ThreeGppNTNSuburbanPropagationLossModel>();
    lossModel->SetAttribute("ShadowingEnabled", BooleanValue(false)); // disable the shadow fading

    for (std::size_t i = 0; i < m_testVectors.GetN(); i++)
    {
        TestVector testVector = m_testVectors.Get(i);

        Vector posA = Vector(0.0, 0.0, 0.0);
        Vector posB = Vector(0.0, 0.0, testVector.m_distance);

        // set the LOS or NLOS condition
        if (testVector.m_isLos)
        {
            lossModel->SetChannelConditionModel(losCondModel);
        }
        else
        {
            lossModel->SetChannelConditionModel(nlosCondModel);
        }

        a->SetPosition(posA);
        b->SetPosition(posB);

        lossModel->SetAttribute("Frequency", DoubleValue(testVector.m_frequency));
        NS_TEST_EXPECT_MSG_EQ_TOL(lossModel->CalcRxPower(testVector.m_pt, a, b),
                                  testVector.m_pr,
                                  m_tolerance,
                                  "Got unexpected rcv power");
    }

    Simulator::Destroy();
}

/**
 * \ingroup propagation-tests
 *
 * Test case for the class ThreeGppNTNRuralPropagationLossModel.
 * It computes the pathloss between two nodes and compares it with the value
 * obtained using the results provided in 3GPP TR 38.821.
 */
class ThreeGppNTNRuralPropagationLossModelTestCase : public TestCase
{
  public:
    /**
     * Constructor
     */
    ThreeGppNTNRuralPropagationLossModelTestCase();

    /**
     * Destructor
     */
    ~ThreeGppNTNRuralPropagationLossModelTestCase() override;

  private:
    /**
     * Build the simulation scenario and run the tests
     */
    void DoRun() override;

    /**
     * Struct containing the parameters for each test
     */
    struct TestVector
    {
        double m_distance;  //!< 2D distance between UT and BS in meters
        bool m_isLos;       //!< if true LOS, if false NLOS
        double m_frequency; //!< carrier frequency in Hz
        double m_pt;        //!< transmitted power in dBm
        double m_pr;        //!< received power in dBm
    };

    TestVectors<TestVector> m_testVectors; //!< array containing all the test vectors
    double m_tolerance;                    //!< tolerance
};

ThreeGppNTNRuralPropagationLossModelTestCase::ThreeGppNTNRuralPropagationLossModelTestCase()
    : TestCase("Test for the ThreeNTNDenseUrbanPropagationLossModel class"),
      m_testVectors(),
      m_tolerance(5e-2)
{
}

ThreeGppNTNRuralPropagationLossModelTestCase::~ThreeGppNTNRuralPropagationLossModelTestCase()
{
}

void
ThreeGppNTNRuralPropagationLossModelTestCase::DoRun()
{
    TestVector testVector;

    testVector.m_distance = 35786000;
    testVector.m_isLos = true;
    testVector.m_frequency = 20.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -209.915;
    m_testVectors.Add(testVector);

    testVector.m_distance = 35786000;
    testVector.m_isLos = true;
    testVector.m_frequency = 30.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -213.437;
    m_testVectors.Add(testVector);

    testVector.m_distance = 35786000;
    testVector.m_isLos = true;
    testVector.m_frequency = 2.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -191.744;
    m_testVectors.Add(testVector);

    testVector.m_distance = 600000;
    testVector.m_isLos = true;
    testVector.m_frequency = 20.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -174.404;
    m_testVectors.Add(testVector);

    testVector.m_distance = 600000;
    testVector.m_isLos = true;
    testVector.m_frequency = 30.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -177.925;
    m_testVectors.Add(testVector);

    testVector.m_distance = 600000;
    testVector.m_isLos = true;
    testVector.m_frequency = 2.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -156.233;
    m_testVectors.Add(testVector);

    testVector.m_distance = 1200000;
    testVector.m_isLos = true;
    testVector.m_frequency = 20.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -180.424;
    m_testVectors.Add(testVector);

    testVector.m_distance = 1200000;
    testVector.m_isLos = true;
    testVector.m_frequency = 30.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -183.946;
    m_testVectors.Add(testVector);

    testVector.m_distance = 1200000;
    testVector.m_isLos = true;
    testVector.m_frequency = 2.0e9;
    testVector.m_pt = 0.0;
    testVector.m_pr = -162.253;
    m_testVectors.Add(testVector);

    // Create the nodes for BS and UT
    NodeContainer nodes;
    nodes.Create(2);

    // Create the mobility models
    Ptr<MobilityModel> a = CreateObject<GeocentricConstantPositionMobilityModel>();
    nodes.Get(0)->AggregateObject(a);
    Ptr<MobilityModel> b = CreateObject<GeocentricConstantPositionMobilityModel>();
    nodes.Get(1)->AggregateObject(b);

    // Use a deterministic channel condition model
    Ptr<ChannelConditionModel> losCondModel = CreateObject<AlwaysLosChannelConditionModel>();
    Ptr<ChannelConditionModel> nlosCondModel = CreateObject<NeverLosChannelConditionModel>();

    // Create the propagation loss model
    Ptr<ThreeGppNTNRuralPropagationLossModel> lossModel =
        CreateObject<ThreeGppNTNRuralPropagationLossModel>();
    lossModel->SetAttribute("ShadowingEnabled", BooleanValue(false)); // disable the shadow fading

    for (std::size_t i = 0; i < m_testVectors.GetN(); i++)
    {
        TestVector testVector = m_testVectors.Get(i);

        Vector posA = Vector(0.0, 0.0, 0.0);
        Vector posB = Vector(0.0, 0.0, testVector.m_distance);

        // set the LOS or NLOS condition
        if (testVector.m_isLos)
        {
            lossModel->SetChannelConditionModel(losCondModel);
        }
        else
        {
            lossModel->SetChannelConditionModel(nlosCondModel);
        }

        a->SetPosition(posA);
        b->SetPosition(posB);

        lossModel->SetAttribute("Frequency", DoubleValue(testVector.m_frequency));
        NS_TEST_EXPECT_MSG_EQ_TOL(lossModel->CalcRxPower(testVector.m_pt, a, b),
                                  testVector.m_pr,
                                  m_tolerance,
                                  "Got unexpected rcv power");
    }

    Simulator::Destroy();
}

/**
 * \ingroup propagation-tests
 *
 * \brief 3GPP NTN Propagation models TestSuite
 *
 * This TestSuite tests the following models:
 *   - ThreeGppNTNDenseUrbanPropagationLossModel
 *   - ThreeGppNTNUrbanPropagationLossModel
 *   - ThreeGppNTNSuburbanPropagationLossModel
 *   - ThreeGppNTNRuralPropagationLossModel
 */
class ThreeGppNTNPropagationLossModelsTestSuite : public TestSuite
{
  public:
    ThreeGppNTNPropagationLossModelsTestSuite();
};

ThreeGppNTNPropagationLossModelsTestSuite::ThreeGppNTNPropagationLossModelsTestSuite()
    : TestSuite("three-gpp-ntn-propagation-loss-model", UNIT)
{
    AddTestCase(new ThreeGppNTNDenseUrbanPropagationLossModelTestCase, TestCase::QUICK);
    AddTestCase(new ThreeGppNTNUrbanPropagationLossModelTestCase, TestCase::QUICK);
    AddTestCase(new ThreeGppNTNSuburbanPropagationLossModelTestCase, TestCase::QUICK);
    AddTestCase(new ThreeGppNTNRuralPropagationLossModelTestCase, TestCase::QUICK);
}

/// Static variable for test initialization
static ThreeGppNTNPropagationLossModelsTestSuite g_propagationLossModelsTestSuite;
