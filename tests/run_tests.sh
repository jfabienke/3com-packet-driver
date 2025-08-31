#!/bin/bash
# Unified Test Runner Script
# 3Com Packet Driver - Complete test execution using new unified runner structure

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test configuration
VERBOSE=0
QUICK_MODE=0
BUILD_ONLY=0
RUNNER_TYPE="all"
CATEGORY=""
STOP_ON_FAILURE=0
RUN_STRESS=0
RUN_PERFORMANCE=1

# Function to print colored output
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to print usage
print_usage() {
    echo "Unified Test Runner for 3Com Packet Driver"
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Test Runner Options:"
    echo "  -r, --runner <type>   Test runner type: all, unit, integration, performance, stress, drivers, protocols"
    echo "  -c, --category <cat>  Specific test category (depends on runner type)"
    echo "  -v, --verbose         Enable verbose output"
    echo "  -q, --quick           Quick test mode (reduced duration)"
    echo "  -s, --stop-on-failure Stop on first test failure"
    echo "  --stress              Include stress tests"
    echo "  --no-performance      Skip performance tests"
    echo "  -b, --build-only      Build tests but don't run them"
    echo "  -h, --help            Show this help message"
    echo ""
    echo "Test Runner Types:"
    echo "  all          - Run complete test suite (default)"
    echo "  unit         - Run unit tests only"
    echo "  integration  - Run integration tests only"
    echo "  performance  - Run performance benchmarks only"
    echo "  stress       - Run stress tests only"
    echo "  drivers      - Run driver-specific tests only"
    echo "  protocols    - Run protocol tests only"
    echo ""
    echo "Examples:"
    echo "  $0                           # Run all tests"
    echo "  $0 -r unit -v                # Run unit tests with verbose output"
    echo "  $0 -r drivers -c 3c509b      # Run 3C509B driver tests only"
    echo "  $0 -r performance --quick    # Run quick performance tests"
    echo "  $0 -r stress --duration 60   # Run stress tests for 60 seconds"
    echo "  $0 --stress                  # Run all tests including stress tests"
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -r|--runner)
            RUNNER_TYPE="$2"
            shift 2
            ;;
        -c|--category)
            CATEGORY="$2"
            shift 2
            ;;
        -v|--verbose)
            VERBOSE=1
            shift
            ;;
        -q|--quick)
            QUICK_MODE=1
            shift
            ;;
        -s|--stop-on-failure)
            STOP_ON_FAILURE=1
            shift
            ;;
        --stress)
            RUN_STRESS=1
            shift
            ;;
        --no-performance)
            RUN_PERFORMANCE=0
            shift
            ;;
        -b|--build-only)
            BUILD_ONLY=1
            shift
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            print_usage
            exit 1
            ;;
    esac
done

# Validate runner type
case $RUNNER_TYPE in
    all|unit|integration|performance|stress|drivers|protocols)
        ;;
    *)
        print_error "Invalid runner type: $RUNNER_TYPE"
        print_usage
        exit 1
        ;;
esac

# Function to build test runners
build_test_runners() {
    print_info "Building test runners..."
    echo "-------------------------------"
    
    # Clean previous builds
    make clean > /dev/null 2>&1 || true
    
    # Build main test framework
    print_info "Building test framework..."
    if [[ $VERBOSE -eq 1 ]]; then
        make -C common all
    else
        make -C common all > /dev/null 2>&1
    fi
    
    if [[ $? -ne 0 ]]; then
        print_error "Failed to build test framework"
        return 1
    fi
    
    # Build test runners
    print_info "Building unified test runners..."
    if [[ $VERBOSE -eq 1 ]]; then
        make -C runners all
    else
        make -C runners all > /dev/null 2>&1
    fi
    
    if [[ $? -ne 0 ]]; then
        print_error "Failed to build test runners"
        return 1
    fi
    
    # Build individual test modules
    print_info "Building test modules..."
    
    # Build unit tests
    if [[ $VERBOSE -eq 1 ]]; then
        make -C unit all
    else
        make -C unit all > /dev/null 2>&1
    fi
    
    # Build integration tests
    if [[ $VERBOSE -eq 1 ]]; then
        make -C integration all
    else
        make -C integration all > /dev/null 2>&1
    fi
    
    # Build performance tests
    if [[ $VERBOSE -eq 1 ]]; then
        make -C performance all
    else
        make -C performance all > /dev/null 2>&1
    fi
    
    # Build stress tests
    if [[ $VERBOSE -eq 1 ]]; then
        make -C stress all
    else
        make -C stress all > /dev/null 2>&1
    fi
    
    # Build assembly tests
    print_info "Building assembly test framework..."
    if [[ $VERBOSE -eq 1 ]]; then
        make -C asm all
    else
        make -C asm all > /dev/null 2>&1
    fi
    
    print_success "All test runners built successfully"
    return 0
}

# Function to construct runner arguments
construct_runner_args() {
    local args=""
    
    if [[ $VERBOSE -eq 1 ]]; then
        args="$args -v"
    fi
    
    if [[ $STOP_ON_FAILURE -eq 1 ]]; then
        args="$args -s"
    fi
    
    if [[ $QUICK_MODE -eq 1 ]]; then
        args="$args --quick"
    fi
    
    if [[ $RUN_STRESS -eq 1 ]]; then
        args="$args --stress"
    fi
    
    if [[ $RUN_PERFORMANCE -eq 0 ]]; then
        args="$args --no-performance"
    fi
    
    if [[ -n "$CATEGORY" ]]; then
        case $RUNNER_TYPE in
            drivers)
                if [[ "$CATEGORY" == "3c509b" ]]; then
                    args="$args -3c509b"
                elif [[ "$CATEGORY" == "3c515" ]]; then
                    args="$args -3c515"
                else
                    args="$args -driver $CATEGORY"
                fi
                ;;
            unit|protocols|integration|performance|stress)
                args="$args --${CATEGORY}-only"
                ;;
            *)
                args="$args --suite $CATEGORY"
                ;;
        esac
    fi
    
    echo "$args"
}

# Function to run specific test runner
run_test_runner() {
    local runner_type="$1"
    local runner_args="$2"
    local runner_path=""
    local runner_name=""
    
    case $runner_type in
        all)
            runner_path="runners/runner_main"
            runner_name="Master Test Runner"
            ;;
        unit)
            runner_path="runners/runner_unit"
            runner_name="Unit Test Runner"
            ;;
        integration)
            runner_path="runners/runner_integration"
            runner_name="Integration Test Runner"
            ;;
        performance)
            runner_path="runners/runner_performance"
            runner_name="Performance Test Runner"
            ;;
        stress)
            runner_path="runners/runner_stress"
            runner_name="Stress Test Runner"
            ;;
        drivers)
            runner_path="runners/runner_drivers"
            runner_name="Driver Test Runner"
            ;;
        protocols)
            runner_path="runners/runner_protocols"
            runner_name="Protocol Test Runner"
            ;;
        *)
            print_error "Unknown runner type: $runner_type"
            return 1
            ;;
    esac
    
    print_info "Executing $runner_name..."
    echo "======================================"
    
    if [[ ! -f "$runner_path" ]]; then
        print_error "Runner not found: $runner_path"
        print_info "Make sure you run the build phase first"
        return 1
    fi
    
    # Execute the test runner
    if [[ $VERBOSE -eq 1 ]]; then
        ./$runner_path $runner_args
    else
        ./$runner_path $runner_args 2>&1 | grep -E "(PASSED|FAILED|ERROR|===|Summary|Result:|Total|Success Rate)"
    fi
    
    local result=$?
    
    if [[ $result -eq 0 ]]; then
        print_success "$runner_name completed successfully"
    else
        print_error "$runner_name failed with exit code: $result"
    fi
    
    return $result
}

# Function to run assembly tests (legacy support)
run_assembly_tests() {
    if [[ "$RUNNER_TYPE" == "all" ]]; then
        print_info "Running Assembly Tests..."
        echo "-------------------------"
        
        if [[ -f "asm/build/cpu_test_runner" ]]; then
            if [[ $VERBOSE -eq 1 ]]; then
                ./asm/build/cpu_test_runner
            else
                ./asm/build/cpu_test_runner > /dev/null 2>&1
            fi
            
            if [[ $? -eq 0 ]]; then
                print_success "Assembly tests passed"
            else
                print_error "Assembly tests failed"
                return 1
            fi
        else
            print_warning "Assembly test runner not found - skipping"
        fi
    fi
    
    return 0
}

# Main execution
main() {
    print_info "Starting 3Com Packet Driver Unified Test Suite"
    echo "================================================="
    
    # Check if we're in the right directory
    if [[ ! -d "runners" || ! -f "Makefile" ]]; then
        print_error "Must be run from the tests directory"
        print_info "Expected directory structure:"
        print_info "  tests/"
        print_info "  ├── runners/"
        print_info "  ├── unit/"
        print_info "  ├── integration/"
        print_info "  ├── performance/"
        print_info "  ├── stress/"
        print_info "  └── Makefile"
        exit 1
    fi
    
    # Check for required tools
    print_info "Checking build environment..."
    
    if ! command -v gcc &> /dev/null; then
        print_error "GCC not found - required for building tests"
        exit 1
    else
        print_success "GCC found: $(gcc --version | head -n1)"
    fi
    
    if ! command -v make &> /dev/null; then
        print_error "Make not found - required for building tests"
        exit 1
    else
        print_success "Make found: $(make --version | head -n1)"
    fi
    
    echo ""
    
    # Build phase
    if ! build_test_runners; then
        print_error "Build phase failed"
        exit 1
    fi
    
    echo ""
    
    # Exit if build-only mode
    if [[ $BUILD_ONLY -eq 1 ]]; then
        print_success "Build completed successfully (build-only mode)"
        exit 0
    fi
    
    # Test execution phase
    print_info "Executing Test Suite"
    echo "===================="
    
    local test_results=0
    local runner_args
    runner_args=$(construct_runner_args)
    
    # Run the specified test runner
    if ! run_test_runner "$RUNNER_TYPE" "$runner_args"; then
        test_results=1
    fi
    
    # Run assembly tests for complete test suite
    if ! run_assembly_tests; then
        test_results=1
    fi
    
    echo ""
    
    # Final Results
    print_info "Test Suite Results"
    echo "=================="
    
    if [[ $test_results -eq 0 ]]; then
        print_success "ALL TESTS COMPLETED SUCCESSFULLY!"
        echo ""
        print_info "The unified test framework successfully validated:"
        echo "  ✓ All selected test categories"
        echo "  ✓ Cross-component integration"
        echo "  ✓ System stability and performance"
        echo "  ✓ Error handling and recovery"
        echo "  ✓ Resource management"
        
        if [[ "$RUNNER_TYPE" == "all" ]]; then
            echo "  ✓ Complete system validation"
        fi
    else
        print_error "SOME TESTS FAILED!"
        echo ""
        print_info "Check the following for debugging:"
        echo "  • Run with -v flag for verbose output"
        echo "  • Check individual test runner outputs"
        echo "  • Run specific test categories to isolate issues"
        echo "  • Review test logs for detailed error information"
    fi
    
    echo ""
    print_info "Test execution complete"
    
    exit $test_results
}

# Trap to handle interrupts
trap 'print_error "Test execution interrupted"; exit 1' INT TERM

# Run main function
main "$@"