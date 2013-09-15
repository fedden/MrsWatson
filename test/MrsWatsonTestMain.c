//
//  MrsWatsonTestMain.c
//  MrsWatson
//
//  Created by Nik Reiman on 8/9/12.
//  Copyright (c) 2012 Teragon Audio. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app/ProgramOption.h"
#include "base/File.h"
#include "base/FileUtilities.h"
#include "base/PlatformUtilities.h"
#include "unit/ApplicationRunner.h"
#include "unit/TestRunner.h"

#include "MrsWatsonTestMain.h"
#include "MrsWatson.h"
#include "logging/LogPrinter.h"

extern LinkedList getTestSuites(void);
extern TestSuite findTestSuite(LinkedList testSuites, const CharString testSuiteName);
extern TestCase findTestCase(TestSuite testSuite, char* testName);
extern void printInternalTests(void);
extern TestSuite runInternalTestSuite(LinkedList testSuites, boolByte onlyPrintFailing);
extern TestSuite runApplicationTestSuite(TestEnvironment testEnvironment);

static const char* DEFAULT_TEST_SUITE_NAME = "all";

#if UNIX
static const char* MRSWATSON_EXE_NAME = "mrswatson";
#elif WINDOWS
static const char* MRSWATSON_EXE_NAME = "mrswatson.exe";
#else
static const char* MRSWATSON_EXE_NAME = "mrswatson";
#endif

static ProgramOptions _newTestProgramOptions(void) {
  ProgramOptions programOptions = newProgramOptions(NUM_TEST_OPTIONS);
  srand((unsigned int)time(NULL));
  
  programOptionsAdd(programOptions, newProgramOptionWithName(OPTION_TEST_SUITE, "suite",
    "Choose a test suite to run. Current suites include:\n\
\t- Application run audio quality tests against actual executable\n\
\t- Internal: run all internal function tests\n\
\t- All: run all tests (default)\n\
\t- A suite name (use '--list' to see all suite names)",
    true, kProgramOptionTypeString, kProgramOptionArgumentTypeRequired));
  programOptionsSetCString(programOptions, OPTION_TEST_SUITE, DEFAULT_TEST_SUITE_NAME);

  programOptionsAdd(programOptions, newProgramOptionWithName(OPTION_TEST_NAME, "test",
    "Run a single test. Tests are named 'Suite:Name', for example:\n\
\t-t 'LinkedList:AppendItem'",
    true, kProgramOptionTypeString, kProgramOptionArgumentTypeRequired));

  programOptionsAdd(programOptions, newProgramOptionWithName(OPTION_TEST_PRINT_TESTS, "list-tests",
    "List all internal tests in the same format required by --test",
    true, kProgramOptionTypeEmpty, kProgramOptionArgumentTypeNone));

  programOptionsAdd(programOptions, newProgramOptionWithName(OPTION_TEST_MRSWATSON_PATH, "mrswatson-path",
    "Path to mrswatson executable. By default, mrswatson is assumed to be in the same \
directory as mrswatsontest. Only required for running application test suite.",
    true, kProgramOptionTypeString, kProgramOptionArgumentTypeRequired));

  programOptionsAdd(programOptions, newProgramOptionWithName(OPTION_TEST_RESOURCES_PATH, "resources",
    "Path to resources directory. Only required for running application test suite.",
    true, kProgramOptionTypeString, kProgramOptionArgumentTypeRequired));

  programOptionsAdd(programOptions, newProgramOptionWithName(OPTION_TEST_PRINT_ONLY_FAILING, "quiet",
    "Print only failing tests. Note that if a test causes the suite to crash, the \
bad test's name will not be printed. In this case, re-run without this option, as \
the test names will be printed before the tests are executed.",
    true, kProgramOptionTypeEmpty, kProgramOptionArgumentTypeNone));

  programOptionsAdd(programOptions, newProgramOptionWithName(OPTION_TEST_KEEP_FILES, "keep-files",
    "Keep files generated by application tests (such as log files, audio output, \
etc.). Normally these files are automatically removed if a test succeeds.",
    true, kProgramOptionTypeEmpty, kProgramOptionArgumentTypeNone));

  programOptionsAdd(programOptions, newProgramOptionWithName(OPTION_TEST_HELP, "help",
    "Print full program help (this screen), or just the help for a single argument.",
    true, kProgramOptionTypeString, kProgramOptionArgumentTypeOptional));

  return programOptions;
}

void _printTestSummary(int testsRun, int testsPassed, int testsFailed, int testsSkipped) {
  CharString numberBuffer = newCharStringWithCapacity(kCharStringLengthShort);

  printToLog(getLogColor(kTestLogEventReset), NULL, "Ran ");
  sprintf(numberBuffer->data, "%d", testsRun);
  printToLog(getLogColor(kTestLogEventSection), NULL, numberBuffer->data);
  printToLog(getLogColor(kTestLogEventReset), NULL, " tests: ");
  sprintf(numberBuffer->data, "%d", testsPassed);
  printToLog(getLogColor(kTestLogEventPass), NULL, numberBuffer->data);
  printToLog(getLogColor(kTestLogEventReset), NULL, " passed, ");

  sprintf(numberBuffer->data, "%d", testsFailed);
  if(testsFailed > 0) {
    printToLog(getLogColor(kTestLogEventFail), NULL, numberBuffer->data);
  }
  else {
    printToLog(getLogColor(kTestLogEventReset), NULL, numberBuffer->data);
  }
  printToLog(getLogColor(kTestLogEventReset), NULL, " failed, ");

  sprintf(numberBuffer->data, "%d", testsSkipped);
  if(testsSkipped > 0) {
    printToLog(getLogColor(kTestLogEventSkip), NULL, numberBuffer->data);
  }
  else {
    printToLog(getLogColor(kTestLogEventReset), NULL, numberBuffer->data);
  }
  printToLog(getLogColor(kTestLogEventReset), NULL, " skipped");
  flushLog(NULL);

  freeCharString(numberBuffer);
}

File _findMrsWatsonExe(CharString mrsWatsonExeArg) {
  CharString currentExecutableFilename = NULL;
  CharString mrsWatsonExeName = NULL;
  File currentExecutablePath = NULL;
  File currentExecutableDir = NULL;
  File mrsWatsonExe = NULL;

  if(mrsWatsonExeArg != NULL && !charStringIsEmpty(mrsWatsonExeArg)) {
    mrsWatsonExe = newFileWithPath(mrsWatsonExeArg);
  }
  else {
    currentExecutableFilename = getExecutablePath();
    currentExecutablePath = newFileWithPath(currentExecutableFilename);
    currentExecutableDir = fileGetParent(currentExecutablePath);
    mrsWatsonExeName = newCharStringWithCString(MRSWATSON_EXE_NAME);
    if(isExecutable64Bit()) {
      charStringAppendCString(mrsWatsonExeName, "64");
    }
    mrsWatsonExe = newFileWithParent(currentExecutableDir, mrsWatsonExeName);
  }

  freeCharString(currentExecutableFilename);
  freeCharString(mrsWatsonExeName);
  freeFile(currentExecutablePath);
  freeFile(currentExecutableDir);
  return mrsWatsonExe;
}

int main(int argc, char* argv[]) {
  ProgramOptions programOptions;
  int totalTestsRun = 0;
  int totalTestsPassed = 0;
  int totalTestsFailed = 0;
  int totalTestsSkipped = 0;
  CharString testSuiteToRun = NULL;
  CharString testSuiteName = NULL;
  CharString mrsWatsonExeName = NULL;
  CharString totalTimeString = NULL;
  CharString executablePath = NULL;
  File mrsWatsonExe = NULL;
  File resourcesPath = NULL;
  boolByte runInternalTests = false;
  boolByte runApplicationTests = false;
  TestCase testCase = NULL;
  TestSuite testSuite = NULL;
  LinkedList testSuites = NULL;
  TestSuite internalTestResults = NULL;
  TestEnvironment testEnvironment = NULL;
  TaskTimer timer;
  char* testArgument;
  char* colon;
  char* testCaseName;

  timer = newTaskTimer(NULL, NULL);
  taskTimerStart(timer);

  programOptions = _newTestProgramOptions();
  if(!programOptionsParseArgs(programOptions, argc, argv)) {
    printf("Or run %s --help (option) to see help for a single option\n", getFileBasename(argv[0]));
    return -1;
  }

  if(programOptions->options[OPTION_TEST_HELP]->enabled) {
    printf("Run with '--help full' to see extended help for all options.\n");
    if(charStringIsEmpty(programOptionsGetString(programOptions, OPTION_TEST_HELP))) {
      printf("All options, where <argument> is required and [argument] is optional\n");
      programOptionsPrintHelp(programOptions, false, DEFAULT_INDENT_SIZE);
    }
    else {
      programOptionsPrintHelp(programOptions, true, DEFAULT_INDENT_SIZE);
    }
    return -1;
  }
  else if(programOptions->options[OPTION_TEST_PRINT_TESTS]->enabled) {
    printInternalTests();
    return -1;
  }

  testSuiteToRun = programOptionsGetString(programOptions, OPTION_TEST_SUITE);
  if(programOptions->options[OPTION_TEST_NAME]->enabled) {
    runInternalTests = false;
    runApplicationTests = false;

    testArgument = programOptionsGetString(programOptions, OPTION_TEST_NAME)->data;
    colon = strchr(testArgument, ':');
    if(colon == NULL) {
      printf("ERROR: Invalid test name");
      programOptionPrintHelp(programOptions->options[OPTION_TEST_NAME], true, DEFAULT_INDENT_SIZE, 0);
      return -1;
    }
    testCaseName = strdup(colon + 1);
    *colon = '\0';

    testSuiteName = programOptionsGetString(programOptions, OPTION_TEST_NAME);
    testSuites = getTestSuites();
    testSuite = findTestSuite(testSuites, testSuiteName);
    if(testSuite == NULL) {
      printf("ERROR: Could not find test suite '%s'\n", testSuiteName->data);
      freeLinkedListAndItems(testSuites, (LinkedListFreeItemFunc)freeTestSuite);
      return -1;
    }
    testCase = findTestCase(testSuite, testCaseName);
    if(testCase == NULL) {
      printf("ERROR: Could not find test case '%s'\n", testCaseName);
      freeLinkedListAndItems(testSuites, (LinkedListFreeItemFunc)freeTestSuite);
      return -1;
    }
    else {
      printf("Running test in %s:\n", testSuite->name);
      runTestCase(testCase, testSuite);
      freeLinkedListAndItems(testSuites, (LinkedListFreeItemFunc)freeTestSuite);
    }
  }
  else if(charStringIsEqualToCString(testSuiteToRun, "all", true)) {
    runInternalTests = true;
    runApplicationTests = true;
  }
  else if(charStringIsEqualToCString(testSuiteToRun, "internal", true)) {
    runInternalTests = true;
  }
  else if(charStringIsEqualToCString(testSuiteToRun, "application", true)) {
    runApplicationTests = true;
  }
  else {
    testSuites = getTestSuites();
    testSuite = findTestSuite(testSuites, testSuiteToRun);
    if(testSuite == NULL) {
      printf("ERROR: Invalid test suite '%s'\n", testSuiteToRun->data);
      printf("Run '%s --list' suite to show possible test suites\n", getFileBasename(argv[0]));
      freeLinkedListAndItems(testSuites, (LinkedListFreeItemFunc)freeTestSuite);
      return -1;
    }
    else {
      testSuite->onlyPrintFailing = programOptions->options[OPTION_TEST_PRINT_ONLY_FAILING]->enabled;
      runTestSuite(testSuite, NULL);
      totalTestsRun = testSuite->numSuccess + testSuite->numFail;
      totalTestsPassed = testSuite->numSuccess;
      totalTestsFailed = testSuite->numFail;
      totalTestsSkipped = testSuite->numSkips;
      freeLinkedListAndItems(testSuites, (LinkedListFreeItemFunc)freeTestSuite);
    }
  }

  if(runInternalTests) {
    printf("=== Internal tests ===\n");
    testSuites = getTestSuites();
    internalTestResults = runInternalTestSuite(testSuites,
      programOptions->options[OPTION_TEST_PRINT_ONLY_FAILING]->enabled);

    totalTestsRun += internalTestResults->numSuccess + internalTestResults->numFail;
    totalTestsPassed += internalTestResults->numSuccess;
    totalTestsFailed += internalTestResults->numFail;
    totalTestsSkipped += internalTestResults->numSkips;

    freeLinkedListAndItems(testSuites, (LinkedListFreeItemFunc)freeTestSuite);
    freeTestSuite(internalTestResults);
  }

  mrsWatsonExe = _findMrsWatsonExe(programOptionsGetString(programOptions, OPTION_TEST_MRSWATSON_PATH));
  if(runApplicationTests && mrsWatsonExe == NULL) {
    printf("Could not find mrswatson, skipping application tests\n");
    runApplicationTests = false;
  }

  if(programOptions->options[OPTION_TEST_RESOURCES_PATH]->enabled) {
    resourcesPath = newFileWithPath(programOptionsGetString(programOptions, OPTION_TEST_RESOURCES_PATH));
  }
  if(runApplicationTests && !fileExists(resourcesPath)) {
    printf("Could not find test resources, skipping application tests\n");
    runApplicationTests = false;
  }

  if(runApplicationTests) {
    printf("\n=== Application tests ===\n");
    testEnvironment = newTestEnvironment(mrsWatsonExe->absolutePath->data, resourcesPath->absolutePath->data);
    testEnvironment->results->onlyPrintFailing = programOptions->options[OPTION_TEST_PRINT_ONLY_FAILING]->enabled;
    testEnvironment->results->keepFiles = programOptions->options[OPTION_TEST_KEEP_FILES]->enabled;
    runApplicationTestSuite(testEnvironment);
    totalTestsRun += testEnvironment->results->numSuccess + testEnvironment->results->numFail;
    totalTestsPassed += testEnvironment->results->numSuccess;
    totalTestsFailed += testEnvironment->results->numFail;
    totalTestsSkipped += testEnvironment->results->numSkips;
  }

  taskTimerStop(timer);
  if(totalTestsRun > 0) {
    printf("\n=== Finished ===\n");
    _printTestSummary(totalTestsRun, totalTestsPassed, totalTestsFailed, totalTestsSkipped);
    totalTimeString = taskTimerHumanReadbleString(timer);
    printf("Total time: %s\n", totalTimeString->data);
  }

  freeTestEnvironment(testEnvironment);
  freeProgramOptions(programOptions);
  freeCharString(executablePath);
  freeCharString(mrsWatsonExeName);
  freeCharString(totalTimeString);
  freeFile(mrsWatsonExe);
  freeFile(resourcesPath);
  freeTaskTimer(timer);
  return totalTestsFailed;
}
