<!-- badges: start -->
[![](https://img.shields.io/badge/License-CC_BY_4.0-lightgrey.svg)](https://creativecommons.org/licenses/by/4.0/)
<!-- badges: end -->

<h1> Development of a Remote Communications Infrastructure for an Off-Grid Energy System: Hardware, Software, and Data </h1>

<b>Contributors</b>  
- Jerun Voeten <a href="https://orcid.org/0009-0009-9822-1285">
<img alt="ORCID logo" src="https://info.orcid.org/wp-content/uploads/2019/11/orcid_16x16.png" width="16" height="16" /> 0009-0009-9822-1285
</a> *author, developer, maintainer*  
- Rebecca Alcock <a href="https://orcid.org/0000-0002-6884-9583">
<img alt="ORCID logo" src="https://info.orcid.org/wp-content/uploads/2019/11/orcid_16x16.png" width="16" height="16" /> 0000-0002-6884-9583
</a> *supervisor*  
- Jakub Tkaczuk <a href="https://orcid.org/0000-0001-7997-9423">
<img alt="ORCID logo" src="https://info.orcid.org/wp-content/uploads/2019/11/orcid_16x16.png" width="16" height="16" /> 0000-0001-7997-9423
</a> *supervisor, maintainer*  
- Elizabeth Tilley <a href="https://orcid.org/0000-0002-2095-9724">
<img alt="ORCID logo" src="https://info.orcid.org/wp-content/uploads/2019/11/orcid_16x16.png" width="16" height="16" /> 0000-0002-2095-9724
</a> *supervisor*  

<br>
<p align="middle"> 
<img src="img/ETH_GHE_logo.svg" width=600>
<br><br>
It compliments the openly-accessible master’s thesis, available on the<br \>  
<a href="">ETH Research Collection</a>.
</p>

# Overview
Monitoring off-grid energy systems in remote locations with unreliable communications is challenging, yet it is essential in health facilities where continuous power supports patient care. Currently, there is no simple end-to-end solution that reliably captures all relevant variables and forwards them autonomously to a site with internet access for analysis and response.

This thesis develops such a system for an off-grid health clinic in Tezhumke, northern Colombia. The design combines existing low-power sensing and long-range radio techniques with practical extensions for constrained clinical sites. In particular, robustness measures and encoding optimizations were added to improve packet delivery over Long Range (LoRa) links, and the overall architecture was engineered to run autonomously from small photovoltaic panels and local power bank storage.

Custom sensor nodes record key measurements: medical refrigerator temperature and humidity, electrical parameters of the off-grid power system, and environmental data. Measurements are transmitted over LoRa and Long Range Wide-Area Network (LoRaWAN) to a gateway, visualized on an Internet of Things (IoT) platform (ThingsBoard), and automatically archived. Alarm rules notify remote researchers and on-site staff via platform alerts, email, SMS, and visual signals when thresholds are exceeded.


In a 7-day field trial, the system achieved a packet reception rate of 97.64\% over a distance of 8.5\,km and demonstrated stable autonomous operation. These results confirm that the system is a mature prototype ready for extended field trials and potential deployment in real-world conditions. Future work should include extended seasonal testing, long-term studies of battery aging, and improvements to the weatherproofing of individual components.

Beyond the immediate use case in Tezhumke, the system may also be applicable to other similarly affected clinics or remote sites. With further refinement, it could support predictive maintenance strategies and contribute to improved reliability of off-grid power supplies, thereby strengthening healthcare delivery in underserved regions.

# Repository
As part of this Master thesis, all developed resources have been collected and systematically documented in this public GitHub repository. The repository serves as a comprehensive reference point for future research and practical applications. It includes:

• Source code for LoRa / LoRaWAN communication,

• ThingsBoard dashboard configuration and the main payload decryption script,

• Dedicated scripts for automated data export,

• PCB layouts and electronic circuit schematics,

• All custom-designed 3D models provided in STL format,

• A complete bill of materials for all hardware components.


