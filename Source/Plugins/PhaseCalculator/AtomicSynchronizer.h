/*
------------------------------------------------------------------

This file is part of a plugin for the Open Ephys GUI
Copyright (C) 2018 Translational NeuroEngineering Laboratory

------------------------------------------------------------------

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#ifndef ATOMIC_SYNCHRONIZER_H_INCLUDED
#define ATOMIC_SYNCHRONIZER_H_INCLUDED

#include <atomic>

/*
 * AtomicSynchronizer: Facilitates the exchange of a resource between two threads
 * safely without using locks. One thread updates the resource and pushes updates
 * using an AtomicWriter; the other only reads the resource, and receives updates
 * when available using the corresponding AtomicReader. Directions for use:
 *
 * - The owner of an AtomicSynchronizer manages the actual objects being exchanged
 *   itself. It should allocate 3 instances of the resource type, probably in an
 *   array. The AtomicReader and AtomicWriter simply instruct their users on which
 *   instance is safe to access at any given time.
 *
 * - After creating an AtomicSynchronizer, one thread should obtain a pointer to its
 *   Reader by calling getReader, and the other can obtain a pointer to the Writer by
 *   getWriter. Each of the Reader and Writer should only be used in ONE thread and are
 *   not themselves thread-safe (but the simultaneous operation of one Reader and one
 *   Writer is safe).
 */

class AtomicSynchronizer {
    friend class Writer;
    friend class Reader;

public:
    class Writer {
        friend class AtomicSynchronizer;
    public:
        Writer() = delete;

        // Get the index of the safe object to write to, if any.
        int getIndexToUse()
        {
            if (index == -1)
            {
                // Attempt to pull an index from readyToWriteIndex
                index = owner->readyToWriteIndex.exchange(-1, std::memory_order_relaxed);
            }
            return static_cast<int>(index);
        }

        // Update readyToReadIndex with newly valid object index, and
        // try to get a new index if one is available.
        void pushUpdate()
        {
            // If readyToReadIndex already contains something, atomic operation ensures
            // that the Reader won't get it if the Writer gets it and vice versa.
            index = owner->readyToReadIndex.exchange(index, std::memory_order_relaxed);

            if (index == -1)
            {
                // Try to get a free index from readyToWriteIndex
                index = owner->readyToWriteIndex.exchange(-1, std::memory_order_relaxed);
            }
        }

    private:
        explicit Writer(AtomicSynchronizer* o)
            : owner (o)
        {}

        AtomicSynchronizer* owner;
        int index;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Writer);
    };

    class Reader {
        friend class AtomicSynchronizer;
    public:
        Reader() = delete;

        // Return the index of the object to use for reading currently.
        // If no valid object has been provided yet, returns -1.
        int pullUpdate()
        {
            // Check readyToReadIndex for newly pushed update
            // If readyToReadIndex already contains something, atomic operation ensures
            // that the Reader won't get it if the Writer gets it and vice versa.
            int newIndex = owner->readyToReadIndex.exchange(-1, std::memory_order_relaxed);

            // Try to clear extraIndex if possible
            if (extraIndex != -1)
            {
                jassert(extraIndex != index);
                jassert(extraIndex != newIndex);
                char expected = -1;
                if (owner->readyToWriteIndex.compare_exchange_strong(expected, extraIndex,
                    std::memory_order_relaxed))
                {
                    extraIndex = -1;
                }
            }

            if (newIndex != -1)
            {
                jassert(newIndex != index);
                if (index != -1)
                {
                    // Great, there's a new update, first have to put current
                    // index somewhere though.

                    // Attempt to put index into readyToWriteIndex
                    char expected = -1;
                    if (!owner->readyToWriteIndex.compare_exchange_strong(expected, index,
                        std::memory_order_relaxed))
                    {
                        // readyToWriteIndex is already occupied
                        // extraIndex must be free at this point. newIndex, index, and
                        // readyToWriteIndex all contain something.
                        jassert(extraIndex == -1);
                        extraIndex = index;
                    }
                }
                index = newIndex;
            }

            return static_cast<int>(index);
        }

    private:
        explicit Reader(AtomicSynchronizer* o)
            : owner (o)
        {}

        AtomicSynchronizer* owner;
        char index;
        char extraIndex; // index of object not in use, if readyToWriteIndex is full

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Reader);
    };


    AtomicSynchronizer()
        : reader    (this)
        , writer    (this)
    {
        reset();
    }

    Writer* getWriter()
    {
        return &writer;
    }

    Reader* getReader()
    {
        return &reader;
    }

    // Reset to state with no valid object
    // No Readers or Writers should be active when this is called!
    void reset()
    {
        readyToReadIndex = -1;
        readyToWriteIndex = 0;
        reader.index = -1;
        reader.extraIndex = 1;
        writer.index = 2;
    }

private:    
    Writer writer;
    Reader reader;

    // shared index slots
    std::atomic<char> readyToReadIndex;  // assigned by Writer; can be read by Reader
    std::atomic<char> readyToWriteIndex; // assigned by Reader; can by modified by Writer

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AtomicSynchronizer)
};

typedef AtomicSynchronizer::Reader* AtomicReaderPtr;
typedef AtomicSynchronizer::Writer* AtomicWriterPtr;

#endif // ATOMIC_SYNCHRONIZER_H_INCLUDED