/*
  Copyright 2013, 2014, 2015, 2019 SINTEF Digital, Mathematics and Cybernetics.
  Copyright 2014 Dr. Blatt - HPC-Simulation-Software & Services
  Copyright 2015, 2017 IRIS AS

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
#include "config.h"

#include <opm/simulators/flow/Main.hpp>
#include  <opm/simulators/linalg/ISTLSolverEbosFlexible.hpp>

namespace Opm {
  namespace Properties {

    namespace TTag {
        struct EclFlowProblemSimple {
            using InheritsFrom = std::tuple<EclFlowProblem>;
        };
    }

    template<class TypeTag>
    struct MatrixAddWellContributions<TypeTag, TTag::EclFlowProblemSimple> {
        static constexpr bool value = true;
    };
    SET_INT_PROP(EclFlowProblemSimple, LinearSolverVerbosity,0);
    SET_SCALAR_PROP(EclFlowProblemSimple, LinearSolverReduction, 1e-2);
    SET_INT_PROP(EclFlowProblemSimple, LinearSolverMaxIter, 100);
    template<class TypeTag>
    struct UseAmg<TypeTag, TTag::EclFlowProblemSimple> { // probably not used
        static constexpr bool value = true;
    };
    template<class TypeTag>
    struct UseCpr<TypeTag, TTag::EclFlowProblemSimple> {
        static constexpr bool value = true;
    };
    SET_INT_PROP(EclFlowProblemSimple, CprMaxEllIter, 1);
    SET_INT_PROP(EclFlowProblemSimple, CprEllSolvetype, 3);
    SET_INT_PROP(EclFlowProblemSimple, CprReuseSetup, 3);
    SET_INT_PROP(EclFlowProblemSimple, CprSolverVerbose, 0);
    template<class TypeTag>
    struct LinearSolverConfiguration<TypeTag, TTag::EclFlowProblemSimple> {
        static constexpr auto value = "ilu0";
    };
    template<class TypeTag>
    struct SystemStrategy<TypeTag, TTag::EclFlowProblemSimple> {
        static constexpr auto value = "quasiimpes";
    };

    SET_PROP(EclFlowProblemSimple, FluidSystem)
    {
    private:
      using Scalar = GetPropType<TypeTag, Properties::Scalar>;
      using Evaluation = GetPropType<TypeTag, Properties::Evaluation>;

    public:
        typedef Opm::BlackOilFluidSystem<Scalar> type;
    };
//    namespace TTag {
//        struct EclFlowProblemSimple {
//            using InheritsFrom = std::tuple<EclBaseProblem, BlackOilModel>;
//        };
//    }
    SET_TYPE_PROP(EclFlowProblemSimple, IntensiveQuantities, Opm::BlackOilIntensiveQuantities<TypeTag>);
    //SET_TYPE_PROP(EclFlowProblemSimple, LinearSolverBackend, Opm::ISTLSolverEbos<TypeTag>);
    //SET_TAG_PROP(EclFlowProblemSimple, LinearSolverSplice, ParallelBiCGStabLinearSolver);
    //SET_TYPE_PROP(EclFlowProblemSimple, LinearSolverBackend, Opm::Linear::ParallelBiCGStabSolverBackend<TypeTag>);//not work
    //SET_TYPE_PROP(EclFlowProblemSimple, LinearSolverBackend, Opm::Linear::SuperLUBackend<TypeTag>)//not work
    //SET_TAG_PROP(EclFlowProblem, FluidState, Opm::BlackOilFluidState);
    SET_TYPE_PROP(EclFlowProblemSimple, LinearSolverBackend, Opm::ISTLSolverEbosFlexible<TypeTag>);
    template<class TypeTag>
    struct EnableStorageCache<TypeTag, TTag::EclFlowProblemSimple> {
        static constexpr bool value = true;
    };
    template<class TypeTag>
    struct EnableIntensiveQuantityCache<TypeTag, TTag::EclFlowProblemSimple> {
        static constexpr bool value = true;
    };

    //SET_INT_PROP(EclFlowProblemSimple, NumWellAdjoint, 1);
//    template<class TypeTag>
//    struct EnableStorageCache<TypeTag, TTag::EclFlowProblem> {
//        static constexpr bool value = true;
//    };
//    template<class TypeTag>
//    struct EnableIntensiveQuantityCache<TypeTag, TTag::EclFlowProblem> {
//        static constexpr bool value = true;
//    };
  }
}

int main(int argc, char** argv)
{
    using TypeTag = Opm::Properties::TTag::EclFlowProblemSimple;
    auto mainObject = Opm::Main(argc, argv);
    return mainObject.runStatic<TypeTag>();
}
