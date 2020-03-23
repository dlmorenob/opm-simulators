/*
  Copyright 2019 SINTEF Digital, Mathematics and Cybernetics.

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef OPM_ISTLSOLVEREBOSFLEXIBLE_HEADER_INCLUDED
#define OPM_ISTLSOLVEREBOSFLEXIBLE_HEADER_INCLUDED

#include <opm/simulators/linalg/matrixblock.hh>
#include <opm/simulators/linalg/findOverlapRowsAndColumns.hpp>
#include <opm/simulators/linalg/FlexibleSolver.hpp>
#include <opm/simulators/linalg/setupPropertyTree.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <memory>
#include <utility>

BEGIN_PROPERTIES

NEW_TYPE_TAG(FlowIstlSolverFlexible, INHERITS_FROM(FlowIstlSolverParams));

NEW_PROP_TAG(GlobalEqVector);
NEW_PROP_TAG(SparseMatrixAdapter);
NEW_PROP_TAG(Simulator);

END_PROPERTIES


namespace Opm
{

//=====================================================================
// Implementation for ISTL-matrix based operator
//=====================================================================
/// This class solves the fully implicit black-oil system by
/// solving the reduced system (after eliminating well variables)
/// as a block-structured matrix (one block for all cell variables) for a fixed
/// number of cell variables.
///
/// The solvers and preconditioners used are run-time configurable.
template <class TypeTag>
class ISTLSolverEbosFlexible
{
    using GridView = typename GET_PROP_TYPE(TypeTag, GridView);
    using SparseMatrixAdapter = typename GET_PROP_TYPE(TypeTag, SparseMatrixAdapter);
    using VectorType = typename GET_PROP_TYPE(TypeTag, GlobalEqVector);
    using Simulator = typename GET_PROP_TYPE(TypeTag, Simulator);
    using Scalar = typename GET_PROP_TYPE(TypeTag, Scalar);
    using MatrixType = typename SparseMatrixAdapter::IstlMatrix;
#if HAVE_MPI
    using Communication = Dune::OwnerOverlapCopyCommunication<int, int>;
#endif
    using SolverType = Dune::FlexibleSolver<MatrixType, VectorType>;

    // for quasiImpesWeights
    typedef typename GET_PROP_TYPE(TypeTag, GlobalEqVector) Vector;
    typedef typename GET_PROP_TYPE(TypeTag, Indices) Indices;
    //typedef typename GET_PROP_TYPE(TypeTag, EclWellModel) WellModel;
    //typedef typename GET_PROP_TYPE(TypeTag, Simulator) Simulator;
    typedef typename SparseMatrixAdapter::IstlMatrix Matrix;
    typedef typename SparseMatrixAdapter::MatrixBlock MatrixBlockType;
    typedef typename Vector::block_type BlockVector;
    typedef typename GET_PROP_TYPE(TypeTag, Evaluation) Evaluation;
    typedef typename GET_PROP_TYPE(TypeTag, ThreadManager) ThreadManager;
    typedef typename GridView::template Codim<0>::Entity Element;
    typedef typename GET_PROP_TYPE(TypeTag, ElementContext) ElementContext;


public:
    static void registerParameters()
    {
        FlowLinearSolverParameters::registerParameters<TypeTag>();
    }

    explicit ISTLSolverEbosFlexible(const Simulator& simulator)
        : simulator_(simulator)
    {
        parameters_.template init<TypeTag>();
        prm_ = setupPropertyTree(parameters_);
        extractParallelGridInformationToISTL(simulator_.vanguard().grid(), parallelInformation_);
        // For some reason simulator_.model().elementMapper() is not initialized at this stage
        // Hence const auto& elemMapper = simulator_.model().elementMapper(); does not work.
        // Set it up manually
        using ElementMapper =
            Dune::MultipleCodimMultipleGeomTypeMapper<GridView>;
        ElementMapper elemMapper(simulator_.vanguard().grid().leafGridView(), Dune::mcmgElementLayout());
        detail::findOverlapAndInterior(simulator_.vanguard().grid(), elemMapper, overlapRows_, interiorRows_);
#if HAVE_MPI
        if (parallelInformation_.type() == typeid(ParallelISTLInformation)) {
            // Parallel case.
            const ParallelISTLInformation* parinfo = std::any_cast<ParallelISTLInformation>(&parallelInformation_);
            assert(parinfo);
            comm_.reset(new Communication(parinfo->communicator()));
        }
#endif
    }

    void eraseMatrix()
    {
    }

    void prepare(SparseMatrixAdapter& mat, VectorType& b)
    {
#if HAVE_MPI
        static bool firstcall = true;
        if (firstcall && parallelInformation_.type() == typeid(ParallelISTLInformation)) {
            // Parallel case.
            const ParallelISTLInformation* parinfo = std::any_cast<ParallelISTLInformation>(&parallelInformation_);
            assert(parinfo);
            const size_t size = mat.istlMatrix().N();
            parinfo->copyValuesTo(comm_->indexSet(), comm_->remoteIndices(), size, 1);
            firstcall = false;
        }
        makeOverlapRowsInvalid(mat.istlMatrix());
#endif
        // Decide if we should recreate the solver or just do
        // a minimal preconditioner update.
        const int newton_iteration = this->simulator_.model().newtonMethod().numIterations();
        bool recreate_solver = false;
        if (this->parameters_.cpr_reuse_setup_ == 0) {
            // Always recreate solver.
            recreate_solver = true;
        } else if (this->parameters_.cpr_reuse_setup_ == 1) {
            // Recreate solver on the first iteration of every timestep.
            if (newton_iteration == 0) {
                recreate_solver = true;
            }
        } else if (this->parameters_.cpr_reuse_setup_ == 2) {
            // Recreate solver if the last solve used more than 10 iterations.
            if (this->iterations() > 10) {
                recreate_solver = true;
            }
        } else {
            assert(this->parameters_.cpr_reuse_setup_ == 3);
            assert(recreate_solver == false);
            // Never recreate solver.
        }

        VectorType weights;

        if( prm_.get<std::string>("preconditioner.type") == "cpr" ||
            prm_.get<std::string>("preconditioner.type") == "cprt"
            )
        {
            bool transpose = false;
            if(prm_.get<std::string>("preconditioner.type") == "cprt"){
                transpose = true;
            }

            if(prm_.get<std::string>("preconditioner.weight_type") == "quasiimpes") {
                if( not( recreate_solver || !solver_) ){
                    // weighs will be created as default in the solver
                    weights = Opm::Amg::getQuasiImpesWeights<MatrixType, VectorType>(
                        mat.istlMatrix(),
                        prm_.get<int>("preconditioner.pressure_var_index"), transpose);
                }

            }else if(prm_.get<std::string>("preconditioner.weight_type") == "trueimpes"  ){
                weights =
                    this->getTrueImpesWeights(b, prm_.get<int>("preconditioner.pressure_var_index"));
                if( recreate_solver || !solver_){
                    // need weights for the constructor
                    prm_.put("preconditioner.weights",weights);
                }
            }else{
                throw std::runtime_error("no such weights implemented for cpr");
            }
        }

        if (recreate_solver || !solver_) {
            if (isParallel()) {
#if HAVE_MPI
                solver_.reset(new SolverType(prm_, mat.istlMatrix(), *comm_));
#endif
            } else {
                solver_.reset(new SolverType(prm_, mat.istlMatrix()));
            }
            rhs_ = b;
        } else {
            solver_->preconditioner().update(weights, prm_.get_child("preconditioner"));
            rhs_ = b;
        }
    }

    bool solve(VectorType& x)
    {
        solver_->apply(x, rhs_, res_);
        return res_.converged;
    }

    bool isParallel() const
    {
#if HAVE_MPI
        return parallelInformation_.type() == typeid(ParallelISTLInformation);
#else
        return false;
#endif
    }

    int iterations() const
    {
        return res_.iterations;
    }

    void setResidual(VectorType& /* b */)
    {
        // rhs_ = &b; // Must be handled in prepare() instead.
    }

    void setMatrix(const SparseMatrixAdapter& /* M */)
    {
        // matrix_ = &M.istlMatrix(); // Must be handled in prepare() instead.
    }

protected:

    /// Zero out off-diagonal blocks on rows corresponding to overlap cells
    /// Diagonal blocks on ovelap rows are set to diag(1.0).
    void makeOverlapRowsInvalid(MatrixType& matrix) const
    {
        //value to set on diagonal
        const int numEq = MatrixType::block_type::rows;
        typename MatrixType::block_type diag_block(0.0);
        for (int eq = 0; eq < numEq; ++eq)
            diag_block[eq][eq] = 1.0;

        //loop over precalculated overlap rows and columns
        for (auto row = overlapRows_.begin(); row != overlapRows_.end(); row++ )
        {
            int lcell = *row;
            // Zero out row.
            matrix[lcell] = 0.0;

            //diagonal block set to diag(1.0).
            matrix[lcell][lcell] = diag_block;
        }
    }

    VectorType getTrueImpesWeights(const VectorType& b,const int pressureVarIndex)
    {
        VectorType weights(b.size());
        ElementContext elemCtx(simulator_);
        Opm::Amg::getTrueImpesWeights(pressureVarIndex, weights, simulator_.vanguard().gridView(),
                                      elemCtx, simulator_.model(),
                                      ThreadManager::threadId());
        return weights;
    }

    const Simulator& simulator_;

    std::unique_ptr<SolverType> solver_;
    FlowLinearSolverParameters parameters_;
    boost::property_tree::ptree prm_;
    VectorType rhs_;
    Dune::InverseOperatorResult res_;
    std::any parallelInformation_;
#if HAVE_MPI
    std::unique_ptr<Communication> comm_;
#endif
    std::vector<int> overlapRows_;
    std::vector<int> interiorRows_;
}; // end ISTLSolverEbosFlexible

} // namespace Opm

#endif // OPM_ISTLSOLVEREBOSFLEXIBLE_HEADER_INCLUDED
