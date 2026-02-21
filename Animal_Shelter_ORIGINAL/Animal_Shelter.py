#!/usr/bin/env python
# coding: utf-8

# In[ ]:


from pymongo import MongoClient
from bson.objectid import ObjectId

class AnimalShelter(object):
    """CRUD operations for the Animal collection in MongoDB"""

    def __init__(self):
        # Initializing the MongoClient. This helps to 
        # access the MongoDB databases and collections.
        # This is hard-wired to use the aac database, the 
        # animals collection, and the aac user.
        # Definitions of the connection string variables are
        # unique to the individual Apporto environment.
        #
        # You must edit the connection variables below to reflect
        # your own instance of MongoDB!
        #
        # Connection Variables
        #
        USER = 'aacuser'
        PASS = 'SNHU1234'
        HOST = 'nv-desktop-services.apporto.com'
        PORT = 31062
        DB = 'AAC'
        COL = 'animals'
        #
        # Initialize Connection
        #
        self.client = MongoClient('mongodb://%s:%s@%s:%d' % (USER,PASS,HOST,PORT))
        self.database = self.client['%s' % (DB)]
        self.collection = self.database['%s' % (COL)]

# Create method inserts new document in the animals collection
    def create(self, data):
        if data is not None:
            result = self.database.animals.insert_one(data)  # data should be dictionary            
            return True if result.acknowledged else False #return boolean TRUE if successful and FALSE if unsuccessful
        else:
            raise Exception("Nothing to save, because data parameter is empty")
            
# Read method uses cursor to query documents and returns results as a list
    def read(self, query):
        if query is None:
            raise ValueError("Empty Query")
        cursor = self.collection.find(query)  # Used find() using the cursor to get documents
        results = list(cursor)  # Converted cursor to a list
        return results
        
# Update method updates documents that match the query and returns number of documents modified
    def update(self, query, update_values):
        if not query:
            raise ValueError("Empty Query")
        if not update_values:
            raise ValueError("Empty Values")
        result = self.collection.update_many(query, {"$set" : update_values}) #uses $set mongo operator
        return result.modified_count
    
# Delete method deletes documents that match the query and returns number of documents deleted
    def delete(self, query):
        if not query:
            raise ValueError("Empty Query")
        result = self.collection.delete_many(query)
        return result.deleted_count


# In[ ]:




