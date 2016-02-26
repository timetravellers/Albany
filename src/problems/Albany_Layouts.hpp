//*****************************************************************//
//    Albany 3.0:  Copyright 2016 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#ifndef ALBANY_LAYOUTS_HPP
#define ALBANY_LAYOUTS_HPP

#include <vector>
#include <string>

#include "Teuchos_RCP.hpp"

#include "Phalanx.hpp"
#include "Albany_DataTypes.hpp"

namespace Albany {
  /*!
   * \brief Struct to construct and hold DataLayouts
   */
  struct Layouts {

    Layouts(int worksetSize, int  numVertices, int numNodes, int numQPts, int numDim, int vecDim=-1, int numSides=0, int numSideNodes=0, int numSideQPs=0);

    //! Data Layout for scalar quantity that lives at nodes
    Teuchos::RCP<PHX::DataLayout> node_scalar;
    //! Data Layout for scalar quantity that lives at quad points
    Teuchos::RCP<PHX::DataLayout> qp_scalar;
    //! Data Layout for scalar quantity that lives on a cell
    Teuchos::RCP<PHX::DataLayout> cell_scalar;
    //! Data Layout for scalar quantity that lives on a cell
    Teuchos::RCP<PHX::DataLayout> cell_scalar2;
    //! Data Layout for scalar quantity that lives on a side
    Teuchos::RCP<PHX::DataLayout> side_scalar;
    //! Data Layout for vector quantity that lives at nodes
    Teuchos::RCP<PHX::DataLayout> node_vector;
    //! Data Layout for vector quantity that lives at quad points
    Teuchos::RCP<PHX::DataLayout> qp_vector;
    //! Data Layout for vector quantity that lives on a cell
    Teuchos::RCP<PHX::DataLayout> cell_vector;
    //! Data Layout for vector quantity that lives on a side
    Teuchos::RCP<PHX::DataLayout> side_vector;
    //! Data Layout for gradient quantity that lives at nodes
    Teuchos::RCP<PHX::DataLayout> node_gradient;
    //! Data Layout for gradient quantity that lives at quad points
    Teuchos::RCP<PHX::DataLayout> qp_gradient;
    //! Data Layout for gradient quantity that lives on a cell
    Teuchos::RCP<PHX::DataLayout> cell_gradient;
    //! Data Layout for gradient quantity that lives on a side
    Teuchos::RCP<PHX::DataLayout> side_gradient;
    //! Data Layout for tensor quantity that lives at nodes
    Teuchos::RCP<PHX::DataLayout> node_tensor;
    //! Data Layout for tensor quantity that lives at quad points
    Teuchos::RCP<PHX::DataLayout> qp_tensor;
    //! Data Layout for tensor quantity that lives on a cell
    Teuchos::RCP<PHX::DataLayout> cell_tensor;
    //! Data Layout for tensor quantity that lives on a side
    Teuchos::RCP<PHX::DataLayout> side_tensor;
    //! Data Layout for tensor gradient quantity that lives at quad points
    Teuchos::RCP<PHX::DataLayout> qp_tensorgradient;
    //! Data Layout for vector gradient quantity that lives at nodes
    Teuchos::RCP<PHX::DataLayout> node_vecgradient;
    //! Data Layout for vector gradient quantity that lives at quad points
    Teuchos::RCP<PHX::DataLayout> qp_vecgradient;
    //! Data Layout for vector gradient quantity that lives on a cell
    Teuchos::RCP<PHX::DataLayout> cell_vecgradient;
    //! Data Layout for vector gradient quantity that lives on a side
    Teuchos::RCP<PHX::DataLayout> side_vecgradient;
    //! Data Layout for third order tensor quantity that lives at nodes
    Teuchos::RCP<PHX::DataLayout> node_tensor3;
    //! Data Layout for third order tensor quantity that lives at quad points
    Teuchos::RCP<PHX::DataLayout> qp_tensor3;
    //! Data Layout for third order tensor quantity that lives on a cell
    Teuchos::RCP<PHX::DataLayout> cell_tensor3;
    //! Data Layout for third order tensor quantity that lives on a side
    Teuchos::RCP<PHX::DataLayout> side_tensor3;
    //! Data Layout for fourth order tensor quantity that lives at nodes
    Teuchos::RCP<PHX::DataLayout> node_tensor4;
    //! Data Layout for fourth order tensor quantity that lives at quad points
    Teuchos::RCP<PHX::DataLayout> qp_tensor4;
    //! Data Layout for fourth order tensor quantity that lives on a cell
    Teuchos::RCP<PHX::DataLayout> cell_tensor4;
    //! Data Layout for fourth order tensor quantity that lives on a side
    Teuchos::RCP<PHX::DataLayout> side_tensor4;
    //! Data Layout for vector quantity that lives at vertices (coordinates) //FIXME: dont oords live at nodes, not vertices?
    Teuchos::RCP<PHX::DataLayout> vertices_vector;
    //! Data Layout for length 3 quantity  that lives at nodes (shell coordinates)
    Teuchos::RCP<PHX::DataLayout> node_3vector;

    //! Data Layout for a scalar on the side nodes
    Teuchos::RCP<PHX::DataLayout> side_node_scalar;
    //! Data Layout for a vector on the side nodes
    Teuchos::RCP<PHX::DataLayout> side_node_vector;
    //! Data Layout for a scalar on the side quadrature points
    Teuchos::RCP<PHX::DataLayout> side_qp_scalar;
    //! Data Layout for a vector on the side quadrature points
    Teuchos::RCP<PHX::DataLayout> side_qp_vector;
    //! Data Layout for quantity that lives at the quad points of the cell sides with dimension = cellDimension
    Teuchos::RCP<PHX::DataLayout> side_qp_coords;
    //! Data Layout for gradient quantity that lives at the quad points of the cell sides
    Teuchos::RCP<PHX::DataLayout> side_qp_gradient;
    //! Data Layout for tensor quantity that lives at the quad points of the cell sides
    Teuchos::RCP<PHX::DataLayout> side_qp_tensor;
    //! Data Layout for vector quantity that lives at the side vertices (coordinates)
    Teuchos::RCP<PHX::DataLayout> side_vertices_vector;

    //! Data Layout for scalar basis functions
    Teuchos::RCP<PHX::DataLayout> node_qp_scalar;
    //! Data Layout for gradient basis functions
    Teuchos::RCP<PHX::DataLayout> node_qp_gradient;
    Teuchos::RCP<PHX::DataLayout> node_qp_vector; // Old, but incorrect name
    //! Data Layout for side scalar basis function
    Teuchos::RCP<PHX::DataLayout> side_node_qp_scalar;
    //! Data Layout for side gradient basis function
    Teuchos::RCP<PHX::DataLayout> side_node_qp_gradient;

    //! Data Layout for scalar quantity on workset
    Teuchos::RCP<PHX::DataLayout> workset_scalar;
    //! Data Layout for vector quantity on workset
    Teuchos::RCP<PHX::DataLayout> workset_vector;
    //! Data Layout for gradient quantity on workset
    Teuchos::RCP<PHX::DataLayout> workset_gradient;
    //! Data Layout for tensor quantity on workset
    Teuchos::RCP<PHX::DataLayout> workset_tensor;
    //! Data Layout for vector gradient quantity on workset
    Teuchos::RCP<PHX::DataLayout> workset_vecgradient;

    //! Data Layout for scalar quantity that is hosted by nodes
    Teuchos::RCP<PHX::DataLayout> node_node_scalar;
    //! Data Layout for vector quantity that is hosted by nodes
    Teuchos::RCP<PHX::DataLayout> node_node_vector;
    //! Data Layout for tensor quantity that is hosted by nodes
    Teuchos::RCP<PHX::DataLayout> node_node_tensor;
    /*!
     * \brief Dummy Data Layout where one is needed but not accessed
     * For instance, the action of scattering residual data from a
     * Field into the residual vector in the workset struct needs an
     * evaluator, but the evaluator has no natural Field that it computes.
     * So, it computes the Scatter field with this (empty) Dummy layout.
     * Requesting this Dummy Field then activates this evaluator so
     * the action is performed.
     */
    Teuchos::RCP<PHX::DataLayout> shared_param;
    Teuchos::RCP<PHX::DataLayout> dummy;

    // For backward compatibility, and simplicitiy, we want to check if
    // the vector length is the same as the spatial dimension. This
    // assumption is hardwired in mechanics problems and we want to
    // test that it is a valide assumption with this bool.
    bool vectorAndGradientLayoutsAreEquivalent;
  };
}

#endif
