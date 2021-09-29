polaris-cpp
========================================
Polaris is an operation centre that supports multiple programming languages, with high compatibility to different application framework. Polaris-cpp is C++ SDK for Polaris.

## Overview

Polaris-cpp provides features listed as below:

* Service instance registration, and health check
   
   Provides API on/offline registration instance information, with regular report to inform caller server's healthy status.

* Service discovery

   Provides multiple API, for users to get a full list of server instance, or get one server instance after route rule filtering and loadbalancing, which can be applied to srevice invocation soon.

* Service circuitbreaking
   
   Provide API to report the invocation result, and conduct circuit breaker instance/group insolation based on collected data, eventually recover when the system allows.

* Service ratelimiting

   Provides API for applications to conduct quota check and deduction, supports rate limit policies that are based on server level and port.

## Quick Start

- [QuickStart](doc/QuickStart.md)
- [Building](doc/Building.md)
- [User Guide](doc/UserGuide.md)
- [Examples](examples/README.md)

Further operation details, please refer to Polaris iWiKi space 

## How to join

We are looking forward to working with you, for instructions regarding code contribution, please refer to [CONTRIBUTING.md](CONTRIBUTING.md)

## License

The polaris-cpp is licensed under the BSD 3-Clause License. Copyright and license information can be found in the file [LICENSE](LICENSE)
