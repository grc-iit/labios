# LABIOS: Label-Based I/O System

<p align="center">
  <img src="https://grc.iit.edu/img/projects/labios/logo.png" alt="LABIOS Logo" width="400"/>
</p>

<p align="center">
  <strong>Revolutionizing I/O for the Convergence of HPC, Big Data, and AI</strong>
</p>

<p align="center">
  <a href="https://github.com/grc-iit/labios/blob/main/LICENSE"><img src="https://img.shields.io/badge/License-BSD%203--Clause-blue.svg" alt="License"></a>
  <a href="https://github.com/grc-iit/labios/releases"><img src="https://img.shields.io/github/v/release/grc-iit/labios" alt="Release"></a>
  <a href="https://github.com/grc-iit/labios/stargazers"><img src="https://img.shields.io/github/stars/grc-iit/labios" alt="Stars"></a>
  <a href="https://grc.iit.edu/docs/iowarp/components/runtime/index"><img src="https://img.shields.io/badge/docs-online-green.svg" alt="Documentation"></a>
  <a href="https://patents.google.com/patent/US11630834B2/en"><img src="https://img.shields.io/badge/Patent-US11630834B2-orange" alt="Patent"></a>
</p>

---

## 🏆 Award-Winning Innovation

LABIOS is an **NSF-funded** (Award #2331480) and **patented** (US Patent 11,630,834 B2) distributed I/O system that introduces a revolutionary label-based paradigm for data management. Think of it as "shipping labels for data" - just as a shipping label contains all information needed to deliver a package, LABIOS labels contain everything needed to process data intelligently across modern computing systems.

## 🚀 Key Features

### Revolutionary Label Abstraction
- **Universal Data Representation**: Convert any I/O request into intelligent, self-describing labels
- **Operation Embedding**: Labels carry both data and operations, enabling computational storage
- **Metadata Rich**: Complete context for intelligent routing and processing

### Production-Ready Performance
- **3x** GPU memory reduction for AI workloads (MegaMmap)
- **10x** lower p99 latency with priority scheduling
- **805x** improved bottleneck detection coverage (WisIO)
- **40%** performance boost for HPC applications (VPIC)

### Enterprise-Grade Architecture
- **Fully Decoupled**: Components can scale independently
- **Storage Agnostic**: Works with POSIX, HDF5, S3, and more
- **AI/ML Optimized**: Native support for model checkpointing and KV caching
- **Production Validated**: Deployed at DOE National Laboratories

## 📦 Quick Start

### Installation via Spack

```bash
# Add LABIOS Spack repository
git clone https://github.com/grc-iit/labios-spack
spack repo add labios-spack

# Install LABIOS
spack install labios

# Load LABIOS environment
spack load labios
```

### Installation from Source

```bash
# Clone repository
git clone https://github.com/grc-iit/labios
cd labios

# Build with CMake
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/path/to/install
make -j8
make install
```

### Docker Deployment

```bash
# Pull LABIOS container
docker pull grciit/labios:latest

# Run LABIOS server
docker run -d -p 4000:4000 --name labios-server grciit/labios:latest

# Run client application
docker run --link labios-server:labios grciit/labios:latest labios-client
```

## 🎯 Usage Examples

### Basic Label Creation

```cpp
#include <labios/labios.h>

// Create a label for data processing
Label* label = labios_create_label(
    LABIOS_WRITE,           // Operation
    data_buffer,            // Data pointer
    buffer_size,            // Size
    "/path/to/destination"  // Destination
);

// Submit label for asynchronous processing
labios_submit(label);
```

### Jarvis Deployment

```bash
# Scaffold LABIOS configuration
jarvis labios scaffold hpc_deployment

# Initialize LABIOS services
jarvis labios init

# Start LABIOS (automatically scales based on workload)
jarvis labios start

# Monitor performance
jarvis labios status --metrics

# Stop services
jarvis labios stop
```

### Priority-Based I/O

```cpp
// High-priority checkpoint operation
Label* checkpoint = labios_create_label_with_priority(
    LABIOS_WRITE,
    checkpoint_data,
    checkpoint_size,
    "/checkpoints/iteration_1000",
    LABIOS_PRIORITY_HIGH
);

// Low-priority logging
Label* log = labios_create_label_with_priority(
    LABIOS_WRITE,
    log_data,
    log_size,
    "/logs/debug.log",
    LABIOS_PRIORITY_LOW
);
```

## 🔬 Advanced Features

### MegaMmap: Memory-Storage Convergence
Transparently extend memory capacity using storage hierarchy:

```bash
# Enable MegaMmap for out-of-core computation
export LABIOS_MEGAMMAP_ENABLED=1
export LABIOS_MEGAMMAP_TIERS="DRAM:32GB,NVMe:256GB,SSD:1TB"

# Run memory-intensive application
./my_ai_training_app --model-size 100GB
```

### WisIO: Intelligent Performance Analysis
Automated bottleneck detection across your I/O stack:

```bash
# Enable WisIO profiling
wisio start --app ./my_application

# Generate performance report
wisio report --format html --output performance_report.html
```

### Integration with AI Frameworks

```python
import labios
import torch

# Configure LABIOS for PyTorch checkpointing
labios.configure_ml_checkpointing(
    framework="pytorch",
    compression="lz4",
    async_mode=True
)

# Transparent model checkpointing
model = MyLargeModel()
labios.enable_smart_checkpointing(model, interval=1000)
```

## 📊 Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      User Applications                       │
│  (HPC Simulations, AI Training, Big Data Analytics)        │
└────────────────────┬────────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────────┐
│                    LABIOS Client Library                     │
│  • Label Creation  • Metadata Enrichment  • Async Submit    │
└────────────────────┬────────────────────────────────────────┘
                     │ Labels
┌────────────────────▼────────────────────────────────────────┐
│                    Label Dispatcher                          │
│  • Priority Scheduling  • Load Balancing  • QoS Policies    │
└────────────────────┬────────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────────┐
│                    Worker Pool                               │
│  • Elastic Scaling  • GPU Support  • Operation Execution    │
└────────────────────┬────────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────────┐
│                   Storage Backends                           │
│  POSIX | HDF5 | S3 | Lustre | DAOS | Custom                │
└─────────────────────────────────────────────────────────────┘
```

## 🛠️ Ecosystem Components

### Core Components
- **[LABIOS Core](https://github.com/grc-iit/labios)**: Main label-based I/O system
- **[MegaMmap](https://github.com/grc-iit/mega_mmap)**: Tiered memory extension (3x GPU memory savings)
- **[WisIO](https://github.com/grc-iit/wisio)**: Multi-perspective I/O analysis (805x coverage)
- **[HStream](https://github.com/grc-iit/HStream)**: Hierarchical streaming (2x throughput)
- **[Viper](https://github.com/grc-iit/viper)**: DNN model transfer (9x faster)

### Integration Tools
- **[IOWarp Runtime](https://github.com/grc-iit/iowarp)**: Unified I/O interception
- **[Jarvis](https://github.com/grc-iit/jarvis-cd)**: Automated deployment and scaling
- **[ChronoLog](https://github.com/grc-iit/chronolog)**: Distributed log ordering

## 📈 Performance Benchmarks

| Workload Type | Baseline | With LABIOS | Improvement |
|--------------|----------|-------------|-------------|
| VPIC Checkpoint | 100s | 60s | 40% faster |
| AI Model Loading | 45s | 5s | 9x faster |
| Streaming Analytics | 1GB/s | 2GB/s | 2x throughput |
| GPU Memory Usage | 24GB | 8GB | 3x reduction |
| p99 Latency | 100ms | 10ms | 10x lower |

## 🤝 Contributing

We welcome contributions! Please see our [Contributing Guide](CONTRIBUTING.md) for details.

```bash
# Fork and clone
git clone https://github.com/YOUR_USERNAME/labios
cd labios

# Create feature branch
git checkout -b feature/amazing-feature

# Make changes and test
make test

# Submit pull request
git push origin feature/amazing-feature
```

## 📚 Documentation & Resources

- **[Full Documentation](https://grc.iit.edu/docs/iowarp/components/runtime/index)**: Comprehensive guides and API reference
- **[Research Papers](#publications)**: Technical details and evaluations
- **[Tutorial Videos](https://youtube.com/gnosis-research)**: Getting started guides
- **[Community Chat](https://grc.zulipchat.com/)**: Join discussions on Zulip

## 📖 Publications

1. **[IPDPS'25]** J. Ye et al., "Characterizing KV Caching on Transformer Inferences"
2. **[ICS'25]** I. Yildirim et al., "WisIO: Multi-Perspective I/O Analysis"
3. **[SC'24]** L. Logan et al., "MegaMmap: Extending Memory Boundaries for GPUs"
4. **[ICPP'24]** J. Cernuda et al., "HStream: Hierarchical Streaming for HPC"
5. **[US Patent]** A. Kougkas et al., "[Label-Based Data Representation I/O Process and System](https://patents.google.com/patent/US11630834B2/en)"

## 🏢 Industry Adoption

LABIOS is available for commercial licensing through Illinois Tech's technology transfer office.

**Ideal for:**
- Cloud service providers seeking better I/O performance
- HPC centers managing diverse workloads
- AI companies optimizing model training infrastructure
- Storage vendors building next-generation systems

**[Contact us for licensing](https://iit.flintbox.com/technologies/6c1ac748-ff2b-4dcc-b436-055fc692cc6b)**

## 👥 Team

**Principal Investigator**: Dr. Xian-He Sun  
**Co-PI**: Dr. Anthony Kougkas  
**Graduate Students**: Luke Logan, Jaime Cernuda, Jie Ye, Izzet Yildirim, Rajni Pawar

## 🙏 Acknowledgments

This material is based upon work supported by the **National Science Foundation** under Grant No. 2331480. Special thanks to our partners at DOE National Laboratories (Argonne, LLNL, Sandia) for their collaboration and support.

## 📄 License

LABIOS is released under the [BSD 3-Clause License](LICENSE). Commercial use requires additional licensing.

---

<p align="center">
  <strong>Ready to revolutionize your I/O?</strong><br>
  <a href="https://github.com/grc-iit/labios">⭐ Star us on GitHub</a> • 
  <a href="https://grc.iit.edu/research/projects/labios">🌐 Visit Project Page</a> • 
  <a href="mailto:akougkas@illinoistech.edu">📧 Contact Team</a>
</p>
