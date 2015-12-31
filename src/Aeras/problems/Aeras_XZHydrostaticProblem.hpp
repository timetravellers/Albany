//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#ifndef AERAS_XZHYDROSTATICPROBLEM_HPP
#define AERAS_XZHYDROSTATICPROBLEM_HPP

#include "Teuchos_RCP.hpp"
#include "Teuchos_ParameterList.hpp"

#include "Albany_AbstractProblem.hpp"

#include "Phalanx.hpp"
#include "PHAL_Workset.hpp"
#include "PHAL_Dimension.hpp"
#include "Aeras_Layouts.hpp"
#include "Aeras_GatherSolution.hpp"
#include "Aeras_ScatterResidual.hpp"
#include "Aeras_DOFInterpolation.hpp"
#include "Aeras_DOFInterpolationLevels.hpp"
#include "Aeras_DOFVecInterpolationLevels.hpp"
#include "Aeras_DOFGradInterpolation.hpp"
#include "Aeras_DOFDivInterpolationLevels.hpp"
#include "Aeras_DOFDInterpolationLevels.hpp"
#include "Aeras_DOFGradInterpolationLevels.hpp"
#include "Aeras_Atmosphere_Moisture.hpp"
#include "Aeras_XZHydrostatic_Density.hpp"
#include "Aeras_XZHydrostatic_EtaDotPi.hpp"
#include "Aeras_XZHydrostatic_GeoPotential.hpp"
#include "Aeras_XZHydrostatic_Omega.hpp"
#include "Aeras_XZHydrostatic_Pressure.hpp"
#include "Aeras_XZHydrostatic_VelResid.hpp"
#include "Aeras_XZHydrostatic_TracerResid.hpp"
#include "Aeras_XZHydrostatic_TemperatureResid.hpp"
#include "Aeras_XZHydrostatic_PiVel.hpp"
#include "Aeras_XZHydrostatic_SPressureResid.hpp"
#include "Aeras_XZHydrostatic_SurfaceGeopotential.hpp"
#include "Aeras_XZHydrostatic_KineticEnergy.hpp"
#include "Aeras_XZHydrostatic_UTracer.hpp"
#include "Aeras_XZHydrostatic_VirtualT.hpp"


namespace Aeras {

  /*!
   * \brief Abstract interface for representing a 1-D finite element
   * problem.
   */
  class XZHydrostaticProblem : public Albany::AbstractProblem {
  public:
  
    //! Default constructor
    XZHydrostaticProblem(const Teuchos::RCP<Teuchos::ParameterList>& params,
		 const Teuchos::RCP<ParamLib>& paramLib,
		 const int numDim_);

    //! Destructor
    ~XZHydrostaticProblem();

    //! Return number of spatial dimensions
    virtual int spatialDimension() const { return numDim; }

    //! Build the PDE instantiations, boundary conditions, and initial solution
    virtual void buildProblem(
      Teuchos::ArrayRCP<Teuchos::RCP<Albany::MeshSpecsStruct> >  meshSpecs,
      Albany::StateManager& stateMgr);

    // Build evaluators
    virtual Teuchos::Array< Teuchos::RCP<const PHX::FieldTag> >
    buildEvaluators(
      PHX::FieldManager<PHAL::AlbanyTraits>& fm0,
      const Albany::MeshSpecsStruct& meshSpecs,
      Albany::StateManager& stateMgr,
      Albany::FieldManagerChoice fmchoice,
      const Teuchos::RCP<Teuchos::ParameterList>& responseList);

    //! Each problem must generate it's list of valid parameters
    Teuchos::RCP<const Teuchos::ParameterList>
    getValidProblemParameters() const;

  private:

    //! Private to prohibit copying
    XZHydrostaticProblem(const XZHydrostaticProblem&);
    
    //! Private to prohibit copying
    XZHydrostaticProblem& operator=(const XZHydrostaticProblem&);

  public:

    //! Main problem setup routine. Not directly called, but
    //! indirectly by following functions
    template <typename EvalT> Teuchos::RCP<const PHX::FieldTag>
    constructEvaluators(
      PHX::FieldManager<PHAL::AlbanyTraits>& fm0,
      const Albany::MeshSpecsStruct& meshSpecs,
      Albany::StateManager& stateMgr,
      Albany::FieldManagerChoice fmchoice,
      const Teuchos::RCP<Teuchos::ParameterList>& responseList);

    void constructDirichletEvaluators(const Albany::MeshSpecsStruct& meshSpecs);
    void constructNeumannEvaluators(const Teuchos::RCP<Albany::MeshSpecsStruct>& meshSpecs);

  protected:
    Teuchos::RCP<Aeras::Layouts> dl;
    const Teuchos::ArrayRCP<std::string> dof_names_tracers;
    const int numDim;
    const int numLevels;
    const int numTracers;
  };

}

#include "Intrepid2_FieldContainer.hpp"
#include "Intrepid2_DefaultCubatureFactory.hpp"
#include "Shards_CellTopology.hpp"

#include "Aeras_Eta.hpp"
#include "Albany_Utils.hpp"
#include "Albany_ProblemUtils.hpp"
#include "Albany_EvaluatorUtils.hpp"
#include "Aeras/responses/Aeras_LayeredResponseUtilities.hpp"
#include "PHAL_Neumann.hpp"

template <typename EvalT>
Teuchos::RCP<const PHX::FieldTag>
Aeras::XZHydrostaticProblem::constructEvaluators(
  PHX::FieldManager<PHAL::AlbanyTraits>& fm0,
  const Albany::MeshSpecsStruct& meshSpecs,
  Albany::StateManager& stateMgr,
  Albany::FieldManagerChoice fieldManagerChoice,
  const Teuchos::RCP<Teuchos::ParameterList>& responseList)
{
  using Teuchos::RCP;
  using Teuchos::rcp;
  using Teuchos::ParameterList;
  using PHX::DataLayout;
  using PHX::MDALayout;
  using std::vector;
  using std::string;
  using std::map;
  using PHAL::AlbanyTraits;
  
  {
    Teuchos::ParameterList& xzhydrostatic_params = params->sublist("XZHydrostatic Problem");
    const typename EvalT::ScalarT Ptop = xzhydrostatic_params.get<double>("Ptop", 101.325); 
    const typename EvalT::ScalarT P0   = xzhydrostatic_params.get<double>("P0",   101325.0); 
    Eta<EvalT>::self(Ptop, P0, numLevels);
  }


  RCP<Intrepid2::Basis<RealType, Intrepid2::FieldContainer<RealType> > >
    intrepidBasis = Albany::getIntrepid2Basis(meshSpecs.ctd);
  RCP<shards::CellTopology> cellType = rcp(new shards::CellTopology (&meshSpecs.ctd));
  
  const int numNodes = intrepidBasis->getCardinality();
  const int worksetSize = meshSpecs.worksetSize;
  
  Intrepid2::DefaultCubatureFactory<RealType> cubFactory;
  RCP <Intrepid2::Cubature<RealType> > cubature = cubFactory.create(*cellType, meshSpecs.cubatureDegree);
  
  const int numQPts = cubature->getNumPoints();
  const int numVertices = meshSpecs.ctd.node_count;
  
  *out << "Field Dimensions: Workset=" << worksetSize 
       << ", Vertices  = " << numVertices
       << ", Nodes     = " << numNodes
       << ", QuadPts   = " << numQPts
       << ", Dim       = " << numDim 
       << ", Neq       = " << neq 
       << ", VecDim    = " << 1 
       << ", numLevels = " << numLevels 
       << ", numTracers= " << numTracers << std::endl;
  
  if (numNodes != numQPts) { 
    TEUCHOS_TEST_FOR_EXCEPTION(true,
         Teuchos::Exceptions::InvalidParameter, "Aeras::XZHydrostaticProblem must be run such that nNodes == numQPts!  " 
         <<  "This does not hold: numNodes = " <<  numNodes << ", numQPts = " << numQPts << "."); 
  }

  //Evaluators for DOFs that depend on levels
  dl = rcp(new Aeras::Layouts(worksetSize,numVertices,numNodes,numQPts,numDim, 1, numLevels));
  Albany::EvaluatorUtils<EvalT, PHAL::AlbanyTraits> evalUtils(dl);

  // Temporary variable used numerous times below
  Teuchos::RCP<PHX::Evaluator<AlbanyTraits> > ev;

  // Define Node Field Names
  Teuchos::ArrayRCP<std::string> dof_names_nodes(1);
  Teuchos::ArrayRCP<std::string> dof_names_nodes_dot(1);
  Teuchos::ArrayRCP<std::string> dof_names_nodes_gradient(1);
  Teuchos::ArrayRCP<std::string> dof_names_nodes_resid(1);
  dof_names_nodes[0]          = "SPressure";
  dof_names_nodes_dot[0]      = dof_names_nodes[0]+"_dot";
  dof_names_nodes_gradient[0] = dof_names_nodes[0]+"_gradient";
  dof_names_nodes_resid[0]    = dof_names_nodes[0]+"_residual";

  // Define Level Field Names
  int numLevelDOF = 2;
  Teuchos::ArrayRCP<std::string> dof_names_levels(2);
  Teuchos::ArrayRCP<std::string> dof_names_levels_dot(2);
  Teuchos::ArrayRCP<std::string> dof_names_levels_gradient(2);
  Teuchos::ArrayRCP<std::string> dof_names_levels_src(2);
  Teuchos::ArrayRCP<std::string> dof_names_levels_resid(2);
  dof_names_levels[0]          = "Velx";
  dof_names_levels[1]          = "Temperature";

  for (int i=0; i<2; ++i) {
    dof_names_levels_dot[i]      = dof_names_levels[i]+"_dot";
    dof_names_levels_gradient[i] = dof_names_levels[i]+"_gradient";
    dof_names_levels_src  [i]    = dof_names_levels[i]+"_source";
    dof_names_levels_resid[i]    = dof_names_levels[i]+"_residual";
  }

  // Define Tracer Field Names
  Teuchos::ArrayRCP<std::string> dof_names_tracers_dot(numTracers);
  Teuchos::ArrayRCP<std::string> dof_names_tracers_gradient(numTracers);
  Teuchos::ArrayRCP<std::string> dof_names_tracers_src(numTracers);
  Teuchos::ArrayRCP<std::string> dof_names_tracers_resid(numTracers);
  Teuchos::ArrayRCP<std::string> dof_names_tracers_deta(numTracers);

  for (int t=0; t<numTracers; ++t) {
    dof_names_tracers_dot     [t] = dof_names_tracers[t]+"_dot";
    dof_names_tracers_gradient[t] = dof_names_tracers[t]+"_gradient";
    dof_names_tracers_src     [t] = dof_names_tracers[t]+"_source";
    dof_names_tracers_resid   [t] = dof_names_tracers[t]+"_residual";
    dof_names_tracers_deta    [t] = dof_names_tracers[t]+"_deta";
  }
 
  {
    RCP<ParameterList> p = rcp(new ParameterList("DOF Interpolation "+dof_names_nodes[0]));
    p->set<string>("Variable Name", dof_names_nodes[0]);
    p->set<Teuchos::RCP<PHX::DataLayout> >("Nodal Variable Layout",     dl->node_scalar);
    p->set<Teuchos::RCP<PHX::DataLayout> >("Quadpoint Variable Layout", dl->qp_scalar);
    p->set<string>("BF Name", "BF");

    ev = rcp(new Aeras::DOFInterpolation<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  {
    RCP<ParameterList> p = rcp(new ParameterList("DOF Interpolation "+dof_names_nodes_dot[0]));
    p->set<string>("Variable Name", dof_names_nodes_dot[0]);
    p->set<Teuchos::RCP<PHX::DataLayout> >("Nodal Variable Layout",     dl->node_scalar);
    p->set<Teuchos::RCP<PHX::DataLayout> >("Quadpoint Variable Layout", dl->qp_scalar);
    p->set<string>("BF Name", "BF");

    ev = rcp(new Aeras::DOFInterpolation<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  for (int t=0; t<numTracers; ++t) {
    RCP<ParameterList> p = rcp(new ParameterList("Tracer Interpolation "+dof_names_tracers[t]));
    p->set<string>("Variable Name", dof_names_tracers[t]);
    p->set<string>("BF Name", "BF");

    ev = rcp(new Aeras::DOFInterpolation<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  for (int t=0; t<numTracers; ++t) {
    RCP<ParameterList> p = rcp(new ParameterList("Tracer Interpolation "+dof_names_tracers_dot[t]));
    p->set<string>("Variable Name", dof_names_tracers_dot[t]);
    p->set<string>("BF Name", "BF");

    ev = rcp(new Aeras::DOFInterpolation<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  {
    RCP<ParameterList> p = rcp(new ParameterList("DOF Grad Interpolation "+dof_names_nodes_gradient[0]));
    // Input
    p->set<string>("Variable Name",          dof_names_nodes[0]);
    p->set<string>("Gradient BF Name",       "Grad BF");
    p->set<string>("Gradient Variable Name", dof_names_nodes_gradient[0]);

    ev = rcp(new Aeras::DOFGradInterpolation<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  for (int t=0; t<numTracers; ++t) {
    RCP<ParameterList> p = rcp(new ParameterList("Tracer Grad Interpolation "+dof_names_tracers_gradient[t]));
    // Input
    p->set<string>("Variable Name", dof_names_tracers[t]);
    p->set<string>("Gradient BF Name", "Grad BF");
    p->set<string>("Gradient Variable Name", dof_names_tracers_gradient[t]);

    ev = rcp(new Aeras::DOFGradInterpolationLevels<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  fm0.template registerEvaluator<EvalT>
    (evalUtils.constructGatherCoordinateVectorEvaluator());

  fm0.template registerEvaluator<EvalT>
    (evalUtils.constructMapToPhysicalFrameEvaluator(cellType, cubature));

  fm0.template registerEvaluator<EvalT>
    (evalUtils.constructComputeBasisFunctionsEvaluator(cellType, intrepidBasis, cubature));

  { // XZHydrostatic SPressure Resid
    RCP<ParameterList> p = rcp(new ParameterList("Hydrostatic SPressure Resid"));
   
    //Input
    p->set<std::string>("Weighted BF Name", "wBF");
    p->set<std::string>("Pressure QP Time Derivative Variable Name", dof_names_nodes_dot[0]);
    p->set<std::string>("Divergence QP PiVelx",        "Divergence QP PiVelx");
    
    p->set<RCP<ParamLib> >("Parameter Library", paramLib);

    Teuchos::ParameterList& paramList = params->sublist("XZHydrostatic Problem");
    p->set<Teuchos::ParameterList*>("XZHydrostatic Problem", &paramList);

    //Output
    p->set<std::string>("Residual Name", dof_names_nodes_resid[0]);

    ev = rcp(new Aeras::XZHydrostatic_SPressureResid<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  {
    RCP<ParameterList> p = rcp(new ParameterList("DOF Interpolation "+dof_names_levels[0]));
    p->set<string>("Variable Name", dof_names_levels[0]);
    p->set<string>("BF Name", "BF");
    
    ev = rcp(new Aeras::DOFVecInterpolationLevels<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }
    
  {
    RCP<ParameterList> p = rcp(new ParameterList("DOF Interpolation "+dof_names_levels_dot[0]));
    p->set<string>("Variable Name", dof_names_levels_dot[0]);
    p->set<string>("BF Name", "BF");
    
    ev = rcp(new Aeras::DOFVecInterpolationLevels<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }
    
  {
    RCP<ParameterList> p = rcp(new ParameterList("DOF Interpolation "+dof_names_levels[1]));
    p->set<string>("Variable Name", dof_names_levels[1]);
    p->set<string>("BF Name", "BF");
    
    ev = rcp(new Aeras::DOFInterpolationLevels<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }
    
  {
    RCP<ParameterList> p = rcp(new ParameterList("DOF Interpolation "+dof_names_levels_dot[1]));
    p->set<string>("Variable Name", dof_names_levels_dot[1]);
    p->set<string>("BF Name", "BF");
    
    ev = rcp(new Aeras::DOFInterpolationLevels<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }
    
  {
    RCP<ParameterList> p = rcp(new ParameterList("DOF Grad Interpolation "+dof_names_levels[1]));
    // Input
    p->set<string>("Variable Name", dof_names_levels[1]);
    p->set<string>("Gradient BF Name", "Grad BF");
    p->set<string>("Gradient Variable Name", dof_names_levels_gradient[1]);
    
    ev = rcp(new Aeras::DOFGradInterpolationLevels<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  

  {//Level Kinetic Energy 
    RCP<ParameterList> p = rcp(new ParameterList("KinieticEnergy"));
    p->set<string>("Velx", dof_names_levels[0]);
    p->set<string>("Kinetic Energy", "KineticEnergy");
  
    ev = rcp(new Aeras::XZHydrostatic_KineticEnergy<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  {//Gradient Level Kinetic Energy
    RCP<ParameterList> p = rcp(new ParameterList("Grad Kinetic Energy"));
    // Input
    p->set<string>("Variable Name", "KineticEnergy");
    p->set<string>("Gradient BF Name", "Grad BF");
    p->set<string>("Gradient Variable Name", "KineticEnergy_gradient");
  
    ev = rcp(new Aeras::DOFGradInterpolationLevels<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  { // XZHydrostatic Velx Resid
    RCP<ParameterList> p = rcp(new ParameterList("XZHydrostatic_"+dof_names_levels_resid[0]));
   
    //Input
    p->set<std::string>("Weighted BF Name",                 "wBF");
    p->set<std::string>("Weighted Gradient BF Name",        "wGrad BF");
    p->set<std::string>("Gradient QP Kinetic Energy",       "KineticEnergy_gradient");
    p->set<std::string>("Gradient QP GeoPotential",         "Gradient QP GeoPotential");
    p->set<std::string>("QP Time Derivative Variable Name", dof_names_levels_dot[0]);
    p->set<std::string>("QP Density",                       "Density");
    p->set<std::string>("Gradient QP Pressure",             "Gradient QP Pressure");
    p->set<std::string>("EtaDotdVelx",                      "EtaDotdVelx");
    p->set<std::string>("D Vel Name",                       "Component Derivative of Velocity");
    
    p->set<RCP<ParamLib> >("Parameter Library", paramLib);
    Teuchos::ParameterList& paramList = params->sublist("XZHydrostatic Problem");
    p->set<Teuchos::ParameterList*>("XZHydrostatic Problem", &paramList);


    //Output
    p->set<std::string>("Residual Name", dof_names_levels_resid[0]);

    ev = rcp(new Aeras::XZHydrostatic_VelResid<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  { // XZHydrostatic Temperature Resid
    RCP<ParameterList> p = rcp(new ParameterList("XZHydrostatic_TemperatureResidual"));
   
    //Input
    p->set<std::string>("Weighted BF Name",               "wBF");
    p->set<std::string>("Weighted Gradient BF Name",      "wGrad BF");
    p->set<std::string>("QP Temperature",                 dof_names_levels[1]);
    p->set<std::string>("Gradient QP Temperature",        dof_names_levels_gradient[1]);
    p->set<std::string>("QP Time Derivative Temperature", dof_names_levels_dot[1]);
    p->set<std::string>("Temperature Source",             dof_names_levels_src[1]);
    p->set<std::string>("QP Velx",                        dof_names_levels[0]);
    p->set<std::string>("Omega",                          "Omega");
    p->set<std::string>("EtaDotdT",                       "EtaDotdT");
    
    p->set<RCP<ParamLib> >("Parameter Library", paramLib);
    Teuchos::ParameterList& paramList = params->sublist("XZHydrostatic Problem");
    p->set<Teuchos::ParameterList*>("XZHydrostatic Problem", &paramList);

    //Output
    p->set<std::string>("Residual Name", dof_names_levels_resid[1]);

    ev = rcp(new Aeras::XZHydrostatic_TemperatureResid<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }
  { // XZHydrostatic Pressure 
    RCP<ParameterList> p = rcp(new ParameterList("XZHydrostatic_Pressure"));

    p->set<RCP<ParamLib> >("Parameter Library", paramLib);
    Teuchos::ParameterList& paramList = params->sublist("XZHydrostatic Problem");
    p->set<Teuchos::ParameterList*>("XZHydrostatic Problem", &paramList);

    //Input
    p->set<std::string>("Pressure Level 0",   dof_names_nodes[0]);

    //Output
    p->set<std::string>("Pressure",                "Pressure");
    p->set<std::string>("Eta",                     "Eta");
    p->set<std::string>("Pi",                      "Pi");

    ev = rcp(new Aeras::XZHydrostatic_Pressure<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }
  {//QP Pressure
    RCP<ParameterList> p = rcp(new ParameterList("DOF Interpolation Pressure"));
    p->set<string>("Variable Name", "Pressure");
    p->set<string>("BF Name", "BF");
    
    ev = rcp(new Aeras::DOFInterpolation<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }
  {//Gradient QP Pressure
      RCP<ParameterList> p = rcp(new ParameterList("Gradient Pressure"));
      // Input
      p->set<string>("Variable Name"            ,   "Pressure");
      p->set<string>("Gradient BF Name"    ,   "Grad BF");
      p->set<string>("Gradient Variable Name",   "Gradient QP Pressure");
    
      ev = rcp(new Aeras::DOFGradInterpolationLevels<EvalT,AlbanyTraits>(*p,dl));
      fm0.template registerEvaluator<EvalT>(ev);
  }
  {//QP Pi
    RCP<ParameterList> p = rcp(new ParameterList("DOF Interpolation Pi"));
    p->set<string>("Variable Name", "Pi");
    p->set<string>("BF Name", "BF");
    
    ev = rcp(new Aeras::DOFInterpolation<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  { // XZHydrostatic Omega = (R*Tv/Cp*P)*DP/Dt) 
    RCP<ParameterList> p = rcp(new ParameterList("XZHydrostatic_Omega"));

    p->set<RCP<ParamLib> >("Parameter Library", paramLib);
    Teuchos::ParameterList& paramList = params->sublist("XZHydrostatic Problem");
    p->set<Teuchos::ParameterList*>("XZHydrostatic Problem", &paramList);

    //Input
    p->set<string>("QP Velx"               , dof_names_levels[0]);
    p->set<string>("Gradient QP Pressure"  , "Gradient QP Pressure");
    p->set<string>("Density"               , "Density");
    p->set<string>("QP Cpstar"             , "Cpstar");
    p->set<string>("Divergence QP PiVelx"  , "Divergence QP PiVelx");

    //Output
    p->set<std::string>("Omega"            , "Omega");

    ev = rcp(new Aeras::XZHydrostatic_Omega<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }
  //{//QP Omega 
  //  RCP<ParameterList> p = rcp(new ParameterList("DOF Interpolation Omega"));
  //  p->set<string>("Variable Name"  , "Omega");
  //  p->set<string>("BF Name", "BF");
  //  
  //  ev = rcp(new Aeras::DOFInterpolation<EvalT,AlbanyTraits>(*p,dl));
  //  fm0.template registerEvaluator<EvalT>(ev);
  //}


  { // XZHydrostatic Density 
    RCP<ParameterList> p = rcp(new ParameterList("XZHydrostatic_Density"));

    p->set<RCP<ParamLib> >("Parameter Library", paramLib);
    Teuchos::ParameterList& paramList = params->sublist("XZHydrostatic Problem");
    p->set<Teuchos::ParameterList*>("XZHydrostatic Problem", &paramList);

    //Input
    p->set<std::string>("Pressure",           "Pressure");
    p->set<std::string>("VirtualT",        dof_names_levels[1]);
    //Output
    p->set<std::string>("Density",                     "Density");

    ev = rcp(new Aeras::XZHydrostatic_Density<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }
  {//QP Density
    RCP<ParameterList> p = rcp(new ParameterList("DOF Interpolation Density"));
    p->set<string>("Variable Name", "Density");
    p->set<string>("BF Name", "BF");
    
    ev = rcp(new Aeras::DOFInterpolation<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  { // XZHydrostatic Virtual Temperature
    RCP<ParameterList> p = rcp(new ParameterList("XZHydrostatic_VirtualT"));

    p->set<RCP<ParamLib> >("Parameter Library", paramLib);
    Teuchos::ParameterList& paramList = params->sublist("XZHydrostatic Problem");
    p->set<Teuchos::ParameterList*>("XZHydrostatic Problem", &paramList);

    //Input
    p->set<std::string>("Pi",                                "Pi");
    p->set<std::string>("Temperature",        dof_names_levels[1]);
    p->set< Teuchos::ArrayRCP<std::string> >("Tracer Names", dof_names_tracers);
    //Output
    p->set<std::string>("Virtual_Temperature", "VirtualT");
    p->set<std::string>("Cpstar",              "Cpstar");

    ev = rcp(new Aeras::XZHydrostatic_VirtualT<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  {//QP Cpstar
    RCP<ParameterList> p = rcp(new ParameterList("DOF Interpolation Cpstar"));
    p->set<string>("Variable Name", "Cpstar");
    p->set<string>("BF Name", "BF");
    
    ev = rcp(new Aeras::DOFInterpolationLevels<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  {// XZHydrostatic GeoPotential
    RCP<ParameterList> p = rcp(new ParameterList("XZHydrostatic_GeoPotential"));

    p->set<RCP<ParamLib> >("Parameter Library", paramLib);
    Teuchos::ParameterList& paramList = params->sublist("XZHydrostatic Problem");
    p->set<Teuchos::ParameterList*>("XZHydrostatic Problem", &paramList);

    //Input
    p->set<std::string>("Density",             "Density" );
    p->set<std::string>("Eta",                 "Eta"     );
    p->set<std::string>("Pi",                  "Pi"      );
    
    p->set<std::string>("SurfaceGeopotential", "SurfaceGeopotential");

    //Output
    p->set<std::string>("GeoPotential",           "GeoPotential");

    ev = rcp(new Aeras::XZHydrostatic_GeoPotential<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  {// XZHydrostatic Surface GeoPotential
    RCP<ParameterList> p = rcp(new ParameterList("XZHydrostatic_SurfaceGeopotential"));
    
    p->set<RCP<ParamLib> >("Parameter Library", paramLib);
    Teuchos::ParameterList& paramList = params->sublist("XZHydrostatic Problem");
    p->set<Teuchos::ParameterList*>("XZHydrostatic Problem", &paramList);
    
    //Input
    
    //Output
    p->set<std::string>("SurfaceGeopotential", "SurfaceGeopotential");
    
    ev = rcp(new Aeras::XZHydrostatic_SurfaceGeopotential<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }
  
  { //QP GeoPotential
    RCP<ParameterList> p = rcp(new ParameterList("DOF Interpolation GeoPotential"));
    p->set<string>("Variable Name", "GeoPotential");
    p->set<string>("BF Name", "BF");
    
    ev = rcp(new Aeras::DOFInterpolationLevels<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  {//Gradient QP GeoPotential 
      RCP<ParameterList> p = rcp(new ParameterList("Gradient GeoPotential"));
      // Input
      p->set<string>("Variable Name",          "GeoPotential");
      p->set<string>("Gradient BF Name",       "Grad BF");
      p->set<string>("Gradient Variable Name", "Gradient QP GeoPotential");
    
      ev = rcp(new Aeras::DOFGradInterpolationLevels<EvalT,AlbanyTraits>(*p,dl));
      fm0.template registerEvaluator<EvalT>(ev);
  }

  {// XZHydrostatic Pi weighted velocity
    RCP<ParameterList> p = rcp(new ParameterList("XZHydrostatic_PiVel"));

    p->set<RCP<ParamLib> >("Parameter Library", paramLib);
    Teuchos::ParameterList& paramList = params->sublist("XZHydrostatic Problem");
    p->set<Teuchos::ParameterList*>("XZHydrostatic Problem", &paramList);

    //Input
    p->set<std::string>("Pi",            "Pi");
    p->set<std::string>("Velx",          dof_names_levels[0]);
    //Output
    p->set<std::string>("PiVelx",        "PiVelx");

    ev = rcp(new Aeras::XZHydrostatic_PiVel<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }
  {//Gradient Pi weighted Velocity 
    RCP<ParameterList> p = rcp(new ParameterList("Divergence PiVelx"));
    // Input
    p->set<string>("Variable Name",          "PiVelx");
    p->set<string>("Gradient BF Name",       "Grad BF");
    p->set<string>("Divergence Variable Name", "Divergence QP PiVelx");
   
    ev = rcp(new Aeras::DOFDivInterpolationLevels<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }
  {//Compontent Derivative of  Velocity 
    RCP<ParameterList> p = rcp(new ParameterList("Component Derivative of Velx"));
    // Input
    p->set<string>("Variable Name",          "Velx");
    p->set<string>("Gradient BF Name",       "Grad BF");
    p->set<string>("Derivative Variable Name", "Component Derivative of Velocity");
   
    ev = rcp(new Aeras::DOFDInterpolationLevels<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }
  { // XZHydrostatic vertical velocity * Pi
    RCP<ParameterList> p = rcp(new ParameterList("XZHydrostatic_EtaDotPi"));

    p->set<RCP<ParamLib> >("Parameter Library", paramLib);
    Teuchos::ParameterList& paramList = params->sublist("XZHydrostatic Problem");
    p->set<Teuchos::ParameterList*>("XZHydrostatic Problem", &paramList);

    //Input
    p->set<std::string>("Divergence QP PiVelx",     "Divergence QP PiVelx");
    p->set<std::string>("Pressure Dot Level 0",   dof_names_nodes_dot[0]);
    p->set<std::string>("Pi"                    , "Pi");
    p->set<std::string>("QP Velx",                dof_names_levels[0]);
    p->set<std::string>("QP Temperature",         dof_names_levels[1]);
    p->set< Teuchos::ArrayRCP<std::string> >("Tracer Names",        dof_names_tracers);

    //Output
    p->set<std::string>("EtaDotPi",                   "EtaDotPi");
    p->set<std::string>("EtaDotdT",                   "EtaDotdT");
    p->set<std::string>("EtaDotdVelx",                "EtaDotdVelx");
    p->set<std::string>("PiDot",                      "PiDot");
    p->set< Teuchos::ArrayRCP<std::string> >("Tracer EtaDotd Names", dof_names_tracers_deta);
    
    ev = rcp(new Aeras::XZHydrostatic_EtaDotPi<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }
 

  { // XZHydrostatic Atmosphere Moisture Resid
    RCP<ParameterList> p = rcp(new ParameterList("XZHydrostatic_Atmosphere_Moisture"));
    p->set<RCP<ParamLib> >("Parameter Library", paramLib);
    Teuchos::ParameterList& paramList = params->sublist("XZHydrostatic Problem");
    p->set<Teuchos::ParameterList*>("XZHydrostatic Problem", &paramList);
   
    //Input
    p->set<std::string>("Weighted BF Name",              "wBF");
    p->set<std::string>("QP Velx",                        dof_names_levels[0]);
    p->set<std::string>("QP Temperature",                 dof_names_levels[1]);
    p->set<std::string>("QP Pressure",                    "Pressure");
    p->set<std::string>("QP Pi",                          "Pi");
    p->set<std::string>("PiDot",                          "PiDot");
    p->set<std::string>("QP Density",                     "Density");
    p->set<std::string>("QP GeoPotential",                "GeoPotential");
    p->set< Teuchos::ArrayRCP<std::string> >("Tracer Names",        dof_names_tracers);

    //Output
    p->set<std::string>("Temperature Source",             dof_names_levels_src[1]);
    p->set< Teuchos::ArrayRCP<std::string> >("Tracer Source Names", dof_names_tracers_src);
    //p->set<std::string>("Residual Name", dof_names_levels_resid[1]);

    ev = rcp(new Aeras::Atmosphere_Moisture<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  for (int t=0; t<numTracers; ++t) {
    RCP<ParameterList> p = rcp(new ParameterList("XZHydrostatic Tracer Resid"));

    {
      RCP<ParameterList> p = rcp(new ParameterList("DOF Grad Interpolation "+dof_names_tracers[t]));
      // Input
      p->set<string>("Variable Name", dof_names_tracers[t]);
      p->set<string>("Gradient BF Name", "Grad BF");
      p->set<string>("Gradient Variable Name", dof_names_tracers_gradient[t]);
    
      ev = rcp(new Aeras::DOFGradInterpolationLevels<EvalT,AlbanyTraits>(*p,dl));
      fm0.template registerEvaluator<EvalT>(ev);
    }

    {//Level u*Tracer
      RCP<ParameterList> p = rcp(new ParameterList("UTracer"));
      p->set<string>("Velx Name", "Velx");
      p->set<string>("PiVelx",    "PiVelx");
      p->set<string>("Tracer",    dof_names_tracers[t]);
      p->set<string>("UTracer",   "U"+dof_names_tracers[t]);
    
      ev = rcp(new Aeras::XZHydrostatic_UTracer<EvalT,AlbanyTraits>(*p,dl));
      fm0.template registerEvaluator<EvalT>(ev);
    }

    {//Divergence QP UTracer
      RCP<ParameterList> p = rcp(new ParameterList("Divergence UTracer"));
      // Input
      p->set<string>("Variable Name",          "U"+dof_names_tracers[t]);
      p->set<string>("Gradient BF Name",       "Grad BF");
      p->set<string>("Divergence Variable Name", "U"+dof_names_tracers[t]+"_divergence");
    
      ev = rcp(new Aeras::DOFDivInterpolationLevels<EvalT,AlbanyTraits>(*p,dl));
      fm0.template registerEvaluator<EvalT>(ev);
    }

    //Input
    p->set<std::string>("Weighted BF Name",                     "wBF");
    p->set<std::string>("Weighted Gradient BF Name",            "wGrad BF");
    p->set<std::string>("Gradient QP PiTracer",                 dof_names_tracers_gradient[t]);
    p->set<std::string>("QP Time Derivative Variable Name",     dof_names_tracers_dot  [t]);
    p->set<std::string>("Divergence QP UTracer",            "U"+dof_names_tracers      [t]+"_divergence");
    p->set<std::string>("Residual Name",                        dof_names_tracers_resid[t]);
    p->set<std::string>("Tracer Source Name",                   dof_names_tracers_src  [t]);
    p->set<std::string>("Tracer EtaDotd Name",                  dof_names_tracers_deta [t]);

    p->set<RCP<ParamLib> >("Parameter Library", paramLib);

    Teuchos::ParameterList& paramList = params->sublist("XZHydrostatic Problem");
    p->set<Teuchos::ParameterList*>("XZHydrostatic Problem", &paramList);

    //Output

    ev = rcp(new Aeras::XZHydrostatic_TracerResid<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  // Construct Aeras Specific FEM evaluators for Vector equation
  {
    RCP<ParameterList> p = rcp(new ParameterList("Gather Solution"));

    p->set< Teuchos::ArrayRCP<std::string> >("Node Names",                  dof_names_nodes);
    p->set< Teuchos::ArrayRCP<std::string> >("Time Dependent Node Names",   dof_names_nodes_dot);

    Teuchos::ArrayRCP<std::string> vector_level_names(1);
    Teuchos::ArrayRCP<std::string> scalar_level_names(1);
    Teuchos::ArrayRCP<std::string> vector_level_names_dot(1);
    Teuchos::ArrayRCP<std::string> scalar_level_names_dot(1);
    vector_level_names[0]     = dof_names_levels[0];
    scalar_level_names[0]     = dof_names_levels[1];
    vector_level_names_dot[0] = dof_names_levels_dot[0];
    scalar_level_names_dot[0] = dof_names_levels_dot[1];
    p->set< Teuchos::ArrayRCP<std::string> >("Vector Level Names",                 vector_level_names);
    p->set< Teuchos::ArrayRCP<std::string> >("Time Dependent Vector Level Names",  vector_level_names_dot);

    p->set< Teuchos::ArrayRCP<std::string> >("Scalar Level Names",                 scalar_level_names);
    p->set< Teuchos::ArrayRCP<std::string> >("Time Dependent Scalar Level Names",  scalar_level_names_dot);

    p->set< Teuchos::ArrayRCP<std::string> >("Tracer Names",                dof_names_tracers);
    p->set< Teuchos::ArrayRCP<std::string> >("Time Dependent Tracer Names", dof_names_tracers_dot);

    ev = rcp(new Aeras::GatherSolution<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  {
    RCP<ParameterList> p = rcp(new ParameterList("Scatter Residual"));

    p->set< Teuchos::ArrayRCP<string> >("Node Residual Names",   dof_names_nodes_resid);

    Teuchos::ArrayRCP<std::string> vector_level_names(1);
    Teuchos::ArrayRCP<std::string> scalar_level_names(1);
    vector_level_names[0]     = dof_names_levels_resid[0];
    scalar_level_names[0]     = dof_names_levels_resid[1];
    p->set< Teuchos::ArrayRCP<std::string> >("Vector Level Residual Names",        vector_level_names);
    p->set< Teuchos::ArrayRCP<std::string> >("Scalar Level Residual Names",        scalar_level_names);

    p->set< Teuchos::ArrayRCP<string> >("Tracer Residual Names", dof_names_tracers_resid);

    p->set<string>("Scatter Field Name", "Scatter XZHydrostatic");

    ev = rcp(new Aeras::ScatterResidual<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  if (fieldManagerChoice == Albany::BUILD_RESID_FM)  {
    PHX::Tag<typename EvalT::ScalarT> res_tag("Scatter XZHydrostatic", dl->dummy);
    fm0.requireField<EvalT>(res_tag);

  }
  else if (fieldManagerChoice == Albany::BUILD_RESPONSE_FM) {
    Aeras::LayeredResponseUtilities<EvalT, PHAL::AlbanyTraits> respUtils(dl);
    return respUtils.constructResponses(fm0, *responseList, Teuchos::null, stateMgr);
  }


  return  Teuchos::null;
}
#endif // AERAS_XZHYDROSTATICPROBLEM_HPP
