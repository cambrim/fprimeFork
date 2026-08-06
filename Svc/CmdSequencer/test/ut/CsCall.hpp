// ======================================================================
// \title  CsCall.hpp
// \author Auto-generated
// \brief  hpp file for CS_CALL test harness implementation class
//
// \copyright
// Copyright 2009-2024, by the California Institute of Technology.
// ALL RIGHTS RESERVED.  United States Government Sponsorship
// acknowledged.
//
// ======================================================================

#ifndef CS_CALL_HPP
#define CS_CALL_HPP

#include "CmdSequencerTester.hpp"

namespace Svc {

namespace CsCall {

class CmdSequencerTester : public Svc::CmdSequencerTester {
  public:
    // ----------------------------------------------------------------------
    // Constructors
    // ----------------------------------------------------------------------

    //! Construct object CmdSequencerTester
    CmdSequencerTester(const SequenceFiles::File::Format::t a_format =
                           SequenceFiles::File::Format::F_PRIME  //!< The file format to use
    );

  public:
    // ----------------------------------------------------------------------
    // Tests
    // ----------------------------------------------------------------------

    //! Test basic single-level nesting
    void test_cs_call_basic();

    //! Test multi-level nesting (parent -> child -> grandchild)
    void test_cs_call_multi_level();

    //! Test nesting depth limit (max 5 levels)
    void test_cs_call_depth_limit();

    //! Test CS_CALL when not running a sequence
    void test_cs_call_not_running();

    //! Test CS_CALL in MANUAL mode (should reject)
    void test_cs_call_manual_mode();

    //! Test CS_CANCEL clears nested stack
    void test_cs_call_cancel();

    //! Test child sequence load failure
    void test_cs_call_load_failure();

    //! Test state restore accuracy
    void test_cs_call_state_restore();

  private:
    // ----------------------------------------------------------------------
    // Helper methods
    // ----------------------------------------------------------------------

    //! Create a simple sequence file with N immediate commands
    //! \param fileName The name of the file to create
    //! \param numCommands The number of NO_OP commands to include
    void createSimpleSequence(const char* fileName, U32 numCommands);

    //! Create a sequence that calls another sequence via CS_CALL
    //! \param parentFile The parent sequence file name
    //! \param childFile The child sequence file to call
    //! \param commandsBeforeCall Commands before CS_CALL
    //! \param commandsAfterCall Commands after CS_CALL
    void createNestedSequence(const char* parentFile,
                              const char* childFile,
                              U32 commandsBeforeCall,
                              U32 commandsAfterCall);
};

}  // namespace CsCall

}  // namespace Svc

#endif
