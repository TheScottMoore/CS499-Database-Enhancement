import json
import os
import sqlite3
import pandas as pd

# Paths (adjust if needed)
PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JSON_PATH = os.path.join(PROJECT_ROOT, "data", "aac_animals_compass.json")
DB_PATH   = os.path.join(PROJECT_ROOT, "data", "aac_animals.db")

def main():
    # Load JSON
    with open(JSON_PATH, "r", encoding="utf-8") as f:
        raw = json.load(f)

    df = pd.DataFrame(raw)

    # Drop Mongo _id if present
    df = df.drop(columns=["_id"], errors="ignore")

    # Make sure your key column exists (AAC usually has animal_id)
    if "animal_id" not in df.columns:
        raise ValueError("Expected column 'animal_id' not found in JSON. Check your field names.")

    # Coerce age to integer where possible
    if "age_upon_outcome_in_weeks" in df.columns:
        df["age_upon_outcome_in_weeks"] = pd.to_numeric(
            df["age_upon_outcome_in_weeks"], errors="coerce"
        ).astype("Int64")

    # Optional: coerce lat/long if present
    for col in ["location_lat", "location_long"]:
        if col in df.columns:
            df[col] = pd.to_numeric(df[col], errors="coerce")

    # Create DB + table (subset to common columns if they exist)
    conn = sqlite3.connect(DB_PATH)
    cur = conn.cursor()

    cur.execute("""
    CREATE TABLE IF NOT EXISTS animals (
      animal_id TEXT PRIMARY KEY,
      name TEXT,
      animal_type TEXT,
      breed TEXT,
      sex_upon_outcome TEXT,
      age_upon_outcome_in_weeks INTEGER,
      outcome_type TEXT,
      outcome_subtype TEXT,
      datetime TEXT,
      location_lat REAL,
      location_long REAL
    );
    """)

    # Keep only columns that exist in both df and table
    table_cols = [
        "animal_id","name","animal_type","breed","sex_upon_outcome",
        "age_upon_outcome_in_weeks","outcome_type","outcome_subtype",
        "datetime","location_lat","location_long"
    ]
    existing = [c for c in table_cols if c in df.columns]
    df2 = df[existing].copy()

    # Replace table contents (repeatable run)
    cur.execute("DELETE FROM animals;")
    conn.commit()

    df2.to_sql("animals", conn, if_exists="append", index=False)

    # Create performance indexes
    cur.execute("""
      CREATE INDEX IF NOT EXISTS idx_animals_sex_breed_age
      ON animals (sex_upon_outcome, breed, age_upon_outcome_in_weeks);
    """)
    cur.execute("CREATE INDEX IF NOT EXISTS idx_animals_breed ON animals (breed);")
    cur.execute("CREATE INDEX IF NOT EXISTS idx_animals_age ON animals (age_upon_outcome_in_weeks);")

    conn.commit()

    # Quick verification
    count = cur.execute("SELECT COUNT(*) FROM animals;").fetchone()[0]
    print(f"Created DB: {DB_PATH}")
    print(f"Rows inserted: {count}")
    print(f"Columns inserted: {existing}")

    conn.close()

if __name__ == "__main__":
    main()