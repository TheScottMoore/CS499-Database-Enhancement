import sqlite3
from pathlib import Path

class SqlAnimalShelter:
    def __init__(self, db_path: str = None):
        if db_path is None:
            # project_root/data/aac_animals.db
            project_root = Path(__file__).resolve().parents[1]
            db_path = str(project_root / "data" / "aac_animals.db")
        self.db_path = db_path

    def _connect(self):
        conn = sqlite3.connect(self.db_path)
        conn.row_factory = sqlite3.Row  # enables dict-like rows
        return conn

    def read_all(self):
        with self._connect() as conn:
            rows = conn.execute("SELECT * FROM animals;").fetchall()
            return [dict(r) for r in rows]

    def read_filtered(self, rescue_type: str):
        """
        rescue_type: 'water' | 'mountain' | 'disaster'
        Mirrors your original Mongo filters.
        """
        rescue_type = (rescue_type or "").lower().strip()

        if rescue_type == "water":
            breeds = ("Labrador Retriever", "Newfoundland", "Chesapeake Bay Retriever")
            sex = "Intact Female"
            age_min, age_max = 26, 156

        elif rescue_type == "mountain":
            breeds = ("German Shepherd", "Alaskan Malamute", "Bernese Mountain Dog")
            sex = "Intact Male"
            age_min, age_max = 26, 156

        elif rescue_type == "disaster":
            breeds = ("Bloodhound", "Doberman Pinscher", "Rottweiler")
            sex = "Intact Male"
            age_min, age_max = 20, 300

        else:
            return self.read_all()

        placeholders = ",".join(["?"] * len(breeds))
        sql = f"""
            SELECT *
            FROM animals
            WHERE sex_upon_outcome = ?
              AND breed IN ({placeholders})
              AND age_upon_outcome_in_weeks BETWEEN ? AND ?;
        """

        params = (sex, *breeds, age_min, age_max)

        with self._connect() as conn:
            rows = conn.execute(sql, params).fetchall()
            return [dict(r) for r in rows]