//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#include "FELIX_StokesFO.hpp"

#include "Intrepid2_FieldContainer.hpp"
#include "Intrepid2_DefaultCubatureFactory.hpp"
#include "Shards_CellTopology.hpp"
#include "PHAL_FactoryTraits.hpp"
#include "Albany_Utils.hpp"
#include "Albany_ProblemUtils.hpp"
#include <string>


FELIX::StokesFO::
StokesFO( const Teuchos::RCP<Teuchos::ParameterList>& params_,
             const Teuchos::RCP<ParamLib>& paramLib_,
             const int numDim_) :
  Albany::AbstractProblem(params_, paramLib_, numDim_),
  numDim(numDim_)
{
  //Set # of PDEs per node based on the Equation Set.  
  //Equation Set is FELIX by default (2 dofs / node -- usual FELIX Stokes FO).
  std::string eqnSet = params_->sublist("Equation Set").get<std::string>("Type", "FELIX"); 
  if (eqnSet == "FELIX") 
    neq = 2; //FELIX FO Stokes system is a system of 2 PDEs
  else if (eqnSet == "Poisson" || eqnSet == "FELIX X-Z") 
    neq = 1; //1 PDE/node for Poisson or FELIX X-Z physics


  // Set the num PDEs for the null space object to pass to ML
  this->rigidBodyModes->setNumPDEs(neq);

  // the following function returns the problem information required for setting the rigid body modes (RBMs) for elasticity problems
  //written by IK, Feb. 2012
  //Check if we want to give ML RBMs (from parameterlist)
  int numRBMs = params_->get<int>("Number RBMs for ML", 0); 
  bool setRBMs = false;   
  if (numRBMs > 0) {
    setRBMs = true; 
    int numScalar = 0;
    if (numRBMs == 2 || numRBMs == 3) 
      rigidBodyModes->setParameters(neq, numDim, numScalar, numRBMs, setRBMs);
    else
      TEUCHOS_TEST_FOR_EXCEPTION(true,std::logic_error,"The specified number of RBMs " 
                                     << numRBMs << " is not valid!  Valid values are 0, 2 and 3.");
  }

  // Need to allocate a fields in mesh database
  if (params->isParameter("RequiredFields"))
  {
    // Need to allocate a fields in mesh database
    Teuchos::Array<std::string> req = params->get<Teuchos::Array<std::string> > ("Required Fields");
    for (int i(0); i<req.size(); ++i)
      this->requirements.push_back(req[i]);
  }
  else
  {
    this->requirements.push_back("surface_height");
#ifdef CISM_HAS_FELIX
    this->requirements.push_back("xgrad_surface_height"); //ds/dx which can be passed from CISM
    this->requirements.push_back("ygrad_surface_height"); //ds/dy which can be passed from CISM
#endif
    this->requirements.push_back("temperature");
    this->requirements.push_back("basal_friction");
    this->requirements.push_back("thickness");
    this->requirements.push_back("flow_factor");
    this->requirements.push_back("surface_velocity");
    this->requirements.push_back("surface_velocity_rms");
    this->requirements.push_back("bed_topography");
  }
}

FELIX::StokesFO::
~StokesFO()
{
  // Nothing to be done here
}

void
FELIX::StokesFO::
buildProblem(
  Teuchos::ArrayRCP<Teuchos::RCP<Albany::MeshSpecsStruct> >  meshSpecs,
  Albany::StateManager& stateMgr)
{
  using Teuchos::rcp;

 /* Construct All Phalanx Evaluators */
  TEUCHOS_TEST_FOR_EXCEPTION(meshSpecs.size()!=1,std::logic_error,"Problem supports one Material Block");
  fm.resize(1);
  fm[0]  = rcp(new PHX::FieldManager<PHAL::AlbanyTraits>);
  buildEvaluators(*fm[0], *meshSpecs[0], stateMgr, Albany::BUILD_RESID_FM, 
		  Teuchos::null);
  constructDirichletEvaluators(*meshSpecs[0]);
  
  if(meshSpecs[0]->ssNames.size() > 0) // Build a sideset evaluator if sidesets are present
     constructNeumannEvaluators(meshSpecs[0]);
}

Teuchos::Array< Teuchos::RCP<const PHX::FieldTag> >
FELIX::StokesFO::
buildEvaluators(
  PHX::FieldManager<PHAL::AlbanyTraits>& fm0,
  const Albany::MeshSpecsStruct& meshSpecs,
  Albany::StateManager& stateMgr,
  Albany::FieldManagerChoice fmchoice,
  const Teuchos::RCP<Teuchos::ParameterList>& responseList)
{
  // Call constructeEvaluators<EvalT>(*rfm[0], *meshSpecs[0], stateMgr);
  // for each EvalT in PHAL::AlbanyTraits::BEvalTypes
  Albany::ConstructEvaluatorsOp<StokesFO> op(
    *this, fm0, meshSpecs, stateMgr, fmchoice, responseList);
  Sacado::mpl::for_each<PHAL::AlbanyTraits::BEvalTypes> fe(op);
  return *op.tags;
}

void
FELIX::StokesFO::constructDirichletEvaluators(
        const Albany::MeshSpecsStruct& meshSpecs)
{
   // Construct Dirichlet evaluators for all nodesets and names
   std::vector<std::string> dirichletNames(neq);
   for (int i=0; i<neq; i++) {
     std::stringstream s; s << "U" << i;
     dirichletNames[i] = s.str();
   }
   Albany::BCUtils<Albany::DirichletTraits> dirUtils;
   dfm = dirUtils.constructBCEvaluators(meshSpecs.nsNames, dirichletNames,
                                          this->params, this->paramLib);
}

// Neumann BCs
void FELIX::StokesFO::constructNeumannEvaluators (const Teuchos::RCP<Albany::MeshSpecsStruct>& meshSpecs)
{

   // Note: we only enter this function if sidesets are defined in the mesh file
   // i.e. meshSpecs.ssNames.size() > 0

   Albany::BCUtils<Albany::NeumannTraits> nbcUtils;

   // Check to make sure that Neumann BCs are given in the input file

   if(!nbcUtils.haveBCSpecified(this->params)) {
      return;
   }


   // Construct BC evaluators for all side sets and names
   // Note that the string index sets up the equation offset, so ordering is important

   std::vector<std::string> neumannNames(neq + 1);
   Teuchos::Array<Teuchos::Array<int> > offsets;
   offsets.resize(neq + 1);

   neumannNames[0] = "U0";
   offsets[0].resize(1);
   offsets[0][0] = 0;
   offsets[neq].resize(neq);
   offsets[neq][0] = 0;

   if (neq>1){
     neumannNames[1] = "U1";
     offsets[1].resize(1);
     offsets[1][0] = 1;
     offsets[neq][1] = 1;
   }

   if (neq>2){
     neumannNames[2] = "U2";
     offsets[2].resize(1);
     offsets[2][0] = 2;
     offsets[neq][2] = 2;
   }

   neumannNames[neq] = "all";

   // Construct BC evaluators for all possible names of conditions
   // Should only specify flux vector components (dCdx, dCdy, dCdz), or dCdn, not both
   std::vector<std::string> condNames(6); //(dCdx, dCdy, dCdz), dCdn, basal, P, lateral, basal_scalar_field
   Teuchos::ArrayRCP<std::string> dof_names(1);
     dof_names[0] = "Velocity";

   // Note that sidesets are only supported for two and 3D currently
   if(numDim == 2)
    condNames[0] = "(dFluxdx, dFluxdy)";
   else if(numDim == 3)
    condNames[0] = "(dFluxdx, dFluxdy, dFluxdz)";
   else
    TEUCHOS_TEST_FOR_EXCEPTION(true, Teuchos::Exceptions::InvalidParameter,
       std::endl << "Error: Sidesets only supported in 2 and 3D." << std::endl);

   condNames[1] = "dFluxdn";
   condNames[2] = "basal";
   condNames[3] = "P";
   condNames[4] = "lateral";
   condNames[5] = "basal_scalar_field";

   std::vector< Teuchos::RCP<PHX::Evaluator<PHAL::AlbanyTraits> > > extra_evaluators;
   ConstructBasalEvaluatorOp constructor(*this,extra_evaluators);
   Sacado::mpl::for_each<PHAL::AlbanyTraits::BEvalTypes> fe(constructor);

   nfm.resize(1); // FELIX problem only has one element block

   nfm[0] = nbcUtils.constructBCEvaluators(meshSpecs, neumannNames, dof_names, true, 0,
                                           condNames, offsets, dl,
                                           this->params, this->paramLib, extra_evaluators);
}

Teuchos::RCP<const Teuchos::ParameterList>
FELIX::StokesFO::getValidProblemParameters () const
{
  Teuchos::RCP<Teuchos::ParameterList> validPL =
    this->getGenericProblemParams("ValidStokesFOProblemParams");
    
  validPL->set<Teuchos::Array<std::string> > ("Required Fields", Teuchos::Array<std::string>(), "");
  validPL->set<bool>("Ice-Hydrology Coupling", false, "If true, saves basalside quantities needed by the Hydrology model");
  validPL->sublist("Stereographic Map", false, "");
  validPL->sublist("FELIX Viscosity", false, "");
  validPL->sublist("FELIX Basal Friction Coefficient", false, "Parameters needed to compute the basal friction coefficient");
  validPL->sublist("FELIX Surface Gradient", false, "");
  validPL->sublist("Equation Set", false, "");
  validPL->sublist("Body Force", false, "");
  validPL->sublist("FELIX Physical Parameters", false, "");
  validPL->sublist("Parameter Fields", false, "Parameter Fields to be registered");
  return validPL;
}

// Instantiating the homotopy parameter holder class
PHAL_INSTANTIATE_TEMPLATE_CLASS(FELIX::HomotopyParamValue)
