#============================================================================
# Sources.
#============================================================================
set(LIB_SRC
#---
	behaviour/belief_data/ThresholdData.cpp
	behaviour/belief_policies/Threshold.cpp
	behaviour/information_policies/InformationPolicy.cpp
#---
    calendar/Calendar.cpp
#---
    core/Atlas.cpp
    core/Cluster.cpp
    core/ClusterType.cpp
    core/ContactProfile.cpp
    core/Disease.cpp
    core/DiseaseProfile.cpp
    core/Health.cpp
    core/Infector.cpp
    core/LogMode.cpp
#---
    geo/Profile.cpp
#---
    multiregion/TravelModel.cpp
#---
    output/CasesFile.cpp
    output/PersonFile.cpp
    output/SummaryFile.cpp
#---
    output/CasesFile.cpp
    output/VisualizerFile.cpp
    output/VisualizerData.cpp
    output/PersonFile.cpp
    output/SummaryFile.cpp
#---
    pop/Person.cpp
    pop/Population.cpp
    pop/PopulationBuilder.cpp
    pop/Generator.cpp
    pop/Household.cpp
    pop/Model.cpp
#---
    sim/run_stride.cpp
    sim/SimulationConfig.cpp
    sim/Simulator.cpp
    sim/SimulatorBuilder.cpp
#---
    util/InstallDirs.cpp
    util/Parallel.cpp
    util/Signals.cpp
#---
    alias/Alias.cpp
)

if( HDF5_FOUND AND NOT STRIDE_FORCE_NO_HDF5 )
    set(LIB_SRC ${LIB_SRC} 
        checkpoint/CheckPoint.cpp
    )
endif()

set(MAIN_SRC
    sim/main.cpp
)

#============================================================================
# Build & install the (OpenMP enabled if OpenMP available) executable.
#============================================================================
add_library(libstride  OBJECT  ${LIB_SRC})
#target_compile_options(libstride PUBLIC "-flto")
#
add_executable(stride  ${MAIN_SRC} $<TARGET_OBJECTS:libstride> $<TARGET_OBJECTS:trng>)
#target_compile_options(stride PUBLIC "-flto")
#
target_link_libraries(stride ${LIBS})
#set_target_properties(stride PROPERTIES LINK_FLAGS_RELEASE "-flto")
install(TARGETS stride  DESTINATION   ${BIN_INSTALL_LOCATION})

if( HDF5_FOUND AND NOT STRIDE_FORCE_NO_HDF5 )
    set(MAINCP_SRC
        checkpoint/main.cpp
    )
    add_executable(stridecp ${MAINCP_SRC} $<TARGET_OBJECTS:libstride> $<TARGET_OBJECTS:trng>)
    target_link_libraries(stridecp ${LIBS})
    install(TARGETS stridecp DESTINATION ${BIN_INSTALL_LOCATION})
    unset(MAINCP_SRC)
endif()

#============================================================================
# Clean up.
#============================================================================
unset(LIB_SRC)
unset(MAIN_SRC)

#############################################################################
