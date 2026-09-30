// SPDX-License-Identifier: AGPL-3.0-or-later
// MCP DAW - Copyright (C) 2026 Make Tech Pty Ltd (maketech.com.au). See LICENSE.
// Headless test runner (JUCE UnitTest). Exit code != 0 on any failure, so CTest/CI can gate on it.
// Usage: mcpdaw_tests [category]   e.g. "mcpdaw_tests DSP"
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <iostream>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // message manager for anything that needs one
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.setPassesAreLogged (false);

    if (argc > 1) runner.runTestsInCategory (argv[1]);
    else runner.runAllTests();

    int failures = 0, passes = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        const auto* r = runner.getResult (i);
        failures += r->failures;
        passes += r->passes;
        if (r->failures > 0)
            for (const auto& m : r->messages) std::cout << "  FAIL [" << r->unitTestName << " / " << r->subcategoryName << "] " << m << "\n";
    }
    std::cout << "\n" << passes << " checks passed, " << failures << " failed.\n";
    return failures == 0 ? 0 : 1;
}
