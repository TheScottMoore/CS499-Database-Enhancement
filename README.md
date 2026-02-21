# CS499-Database-Enhancement
Enhancement for Database category for CS Capstone

This artifact was originally developed for CS-340: Client/Server Development. The application was written in Python and connected to a MongoDB database to manage records for trained rescue animals sourced from animal shelters. The original purpose of this project within CS-340 was to demonstrate the ability to link the front-end and back-end components into a working web application.

My enhancement was based on converting the original MongoDB database to an SQLite database to demonstrate mastery in creating databases, migrating and sanitizing data, and database indexing. These were all done within this enhancement while preserving the compatibility with the front-end dash system and ensuring it still maintained portability. Also, it leveraged my ability to reorganize my data transfer layer so that it remained a fully functional, full-stack webapplication.

The primary benefits of this were to enforce a strict data structure, simplify deployment by eliminating dependencies, including connections to external data servers and increase maintainability. I also implemented common-sense security upgrades, such as converting hard-coded credentials into an environment-based configuration and implementing error handling and input validation.


Successful execution of data migration script
<img width="802" height="109" alt="Screenshot 2026-02-07 145514" src="https://github.com/user-attachments/assets/77a3b15b-1d07-425e-8c77-6c205c2fbc99" />


Front-end dash displaying data from enhanced SQLite database post-migration.
<img width="1264" height="890" alt="Screenshot 2026-02-07 171607" src="https://github.com/user-attachments/assets/76b79cde-4880-4fd3-902f-92ac1940a1e9" />
