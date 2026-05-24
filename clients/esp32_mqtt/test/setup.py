from setuptools import setup, find_packages

with open("README.md", "r", encoding="utf-8") as fh:
    long_description = fh.read()

setup(
    name="mqtt-bridge-tester",
    version="1.0.0",
    author="Your Name",
    description="MQTT Bridge Device Validation Test for ESP32",
    long_description=long_description,
    long_description_content_type="text/markdown",
    url="https://github.com/yourusername/mqtt-bridge-tester",
    packages=find_packages(),
    classifiers=[
        "Programming Language :: Python :: 3",
        "License :: OSI Approved :: MIT License",
        "Operating System :: OS Independent",
    ],
    python_requires=">=3.7",
    install_requires=[
        "paho-mqtt>=1.6.1",
        "requests>=2.31.0",
        "python-dotenv>=1.0.0",
    ],
    entry_points={
        "console_scripts": [
            "mqtt-bridge-test=mqtt_bridge_test:main",
        ],
    },
)