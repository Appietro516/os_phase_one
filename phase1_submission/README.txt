------------------------------------------------------------------------
Phase 1: README

Anthony Pietrofeso
Bryce Gallion

2/23/2020

CSCV 452
------------------------------------------------------------------------
Summary:
This basic readme that will describe compiling and testing our project code

Compiling:
* The files to compile the code have been created based on the provided sample
Makefile. The makefile is setup as described in the "Phase 1: The Kernel" pdf
on d2l in section 8.
* To compile the code, a hard or symbolic link must be included to the
USLOSS library in the "phase1" folder.

Testing:
* As with the provided code, testcases should be placed into a "testcases"
folder within the "phase1" folder.
* Using the format Make TARGET_TEST will compile the testcase with the written
code for use of Testing,
* For more information on testing, please view the "phase1/self_evaluation_report.txt"
This report will contain a header, and then list of testcases formatted as:
    testXX:
        Self-Evaluation: PASSED/FAIL
        Reason: Why we marked the testcase as either pass or fail, and any insight
                where we believe the output may be logically correct.
        Output: The output we obtained when running the testcase on our machine
